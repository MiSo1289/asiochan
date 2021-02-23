#pragma once

#include <array>
#include <concepts>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <utility>

#include "asiochan/asio.hpp"
#include "asiochan/async_promise.hpp"
#include "asiochan/detail/channel_shared_state.hpp"
#include "asiochan/detail/channel_waiter_list.hpp"
#include "asiochan/detail/send_slot.hpp"
#include "asiochan/select_concepts.hpp"
#include "asiochan/select_result.hpp"

namespace asiochan
{
    namespace detail
    {
        template <select_op... Ops>
        inline constexpr auto select_ops_base_tokens = []() {
            auto token_base = std::size_t{0};
            return std::array{
                std::exchange(token_base, token_base + Ops::num_alternatives)...,
            };
        }();

        template <std::size_t index, select_op... Ops>
        [[nodiscard]] auto select_clear_one(
            std::tuple<Ops&...>& ops,
            std::tuple<typename Ops::wait_state_type...>& ops_wait_states,
            std::size_t const successful_sub_op,
            std::optional<select_result<Ops...>>& result)
            -> asio::awaitable<void>
        {
            auto& op = std::get<index>(ops);
            using Op = std::decay_t<decltype(op)>;

            auto successful_alternative = std::optional<std::size_t>{};
            constexpr auto op_base_token = select_ops_base_tokens<Ops...>[index];

            if (successful_sub_op >= op_base_token
                and successful_sub_op < op_base_token + Op::num_alternatives)
            {
                successful_alternative = successful_sub_op - op_base_token;

                // Set result
                result.emplace(op.get_result(*successful_alternative), successful_sub_op);
            }

            co_await op.clear_wait(successful_alternative, std::get<index>(ops_wait_states));
        }

        template <select_op... Ops, std::size_t... indices>
        [[nodiscard]] auto select_clear(
            std::tuple<Ops&...>& ops,
            std::tuple<typename Ops::wait_state_type...>& ops_wait_states,
            std::size_t const successful_sub_op,
            std::index_sequence<indices...>)
            -> asio::awaitable<select_result<Ops...>>
        {
            auto result = std::optional<select_result<Ops...>>{};
            ((co_await select_clear_one<indices>(
                 ops,
                 ops_wait_states,
                 successful_sub_op,
                 result)),
             ...);
            co_return std::move(*result);
        }
    }  // namespace detail

    // clang-format off
    template <select_op... Ops>
    requires waitable_selection<Ops...>
    [[nodiscard]] auto select(Ops... ops_args) -> asio::awaitable<select_result<Ops...>>
    // clang-format on
    {
        auto executor = co_await asio::this_coro::executor;
        auto result = std::optional<select_result<Ops...>>{};
        auto exception = std::optional<std::exception_ptr>{};

        try
        {
            // Prepare state
            auto ops = std::tie(ops_args...);
            auto strand = std::get<0>(ops).strand();
            auto select_ctx = detail::select_wait_context{};
            auto submit_completion_promise = async_promise<void>{};
            auto submit_completed = false;
            constexpr auto ops_base_tokens = []() {
                auto token_base = std::size_t{0};
                return std::array{
                    std::exchange(token_base, token_base + Ops::num_alternatives)...,
                };
            }();
            auto ops_wait_states = std::tuple<typename Ops::wait_state_type...>{};

            // Acquire the first operation's strand before awaiting on the promise
            co_await asio::dispatch(strand, asio::use_awaitable);

            asio::co_spawn(
                executor,
                ([&]<std::size_t... indices>(std::index_sequence<indices...>)->asio::awaitable<void> {
                    // Submit operations
                    ((co_await std::get<indices>(ops)
                          .submit_with_wait(
                              select_ctx,
                              ops_base_tokens[indices],
                              std::get<indices>(ops_wait_states))
                      == select_waitful_submit_result::completed_waitfree)
                     or ...);

                    // Synchronize with parent routine
                    co_await asio::dispatch(strand, asio::use_awaitable);
                    submit_completed = true;
                    if (submit_completion_promise.valid())
                    {
                        submit_completion_promise.set_value();
                    }
                }(std::index_sequence_for<Ops...>{})),
                asio::detached);

            // Wait until one of the operations succeeds
            auto const successful_sub_op = co_await select_ctx.promise.get_awaitable();

            // Synchronize with submission routine
            co_await asio::dispatch(strand, asio::use_awaitable);
            if (not submit_completed)
            {
                co_await submit_completion_promise.get_awaitable();
            }

            // Clear remaining waits
            result = co_await detail::select_clear(
                ops,
                ops_wait_states,
                successful_sub_op,
                std::index_sequence_for<Ops...>{});
        }
        catch (...)
        {
            exception = std::current_exception();
        }

        // Exit any strand that the operation ended on
        co_await asio::post(executor, asio::use_awaitable);

        assert(result.has_value() or exception.has_value());

        if (not exception)
        {
            co_return std::move(*result);
        }

        std::rethrow_exception(std::move(*exception));
    }

    // clang-format off
    template <select_op... Ops>
    requires waitfree_selection<Ops...>
    [[nodiscard]] auto select(Ops... ops_args) -> asio::awaitable<select_result<Ops...>>
    // clang-format on
    {
        auto executor = co_await asio::this_coro::executor;
        auto result = std::optional<select_result<Ops...>>{};
        auto exception = std::optional<std::exception_ptr>{};

        try
        {
            auto ops = std::tie(ops_args...);
            constexpr auto ops_base_tokens = []() {
                auto token_base = std::size_t{0};
                return std::array{
                    std::exchange(token_base, token_base + Ops::num_alternatives)...,
                };
            }();

            ([&]<std::size_t... indices>(std::index_sequence<indices...>)->asio::awaitable<void> {
                // Submit operations
                (co_await([&](select_op auto& op) -> asio::awaitable<bool> {
                     constexpr auto op_index = indices;

                     auto const successful_alternative = co_await op.submit_if_ready();

                     if (successful_alternative)
                     {
                         result.emplace(op.get_result(), ops_base_tokens[op_index] + *successful_alternative);
                         co_return true;
                     }

                     co_return false;
                 }(std::get<indices>(ops)))
                 or ...);
            }(std::index_sequence_for<Ops...>{}));
        }
        catch (...)
        {
            exception = std::current_exception();
        }

        // Exit any strand that the operation ended on
        co_await asio::post(executor, asio::use_awaitable);

        assert(result.has_value() or exception.has_value());

        if (not exception)
        {
            co_return std::move(*result);
        }

        std::rethrow_exception(std::move(*exception));
    }
}  // namespace asiochan
