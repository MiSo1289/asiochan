#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>

#include "asiochan/asio.hpp"
#include "asiochan/async_promise.hpp"
#include "asiochan/detail/channel_waiter_list.hpp"
#include "asiochan/select_concepts.hpp"
#include "asiochan/select_result.hpp"

namespace asiochan::detail
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

    template <typename... Ops, typename Strand, std::size_t... indices>
    [[nodiscard]] auto select_waitful_submit(
        std::tuple<Ops&...>& ops,
        Strand& strand,
        bool& submit_completed,
        async_promise<void>& submit_completion_promise,
        std::tuple<typename Ops::wait_state_type...>& ops_wait_states,
        select_wait_context& select_ctx,
        std::index_sequence<indices...>)
        -> asio::awaitable<void>
    {
        auto& ops_base_tokens = select_ops_base_tokens<Ops...>;
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
    }

    template <std::size_t op_index, typename... Ops>
    [[nodiscard]] auto select_waitfree_submit_one(
        std::tuple<Ops&...>& ops,
        std::optional<select_result<Ops...>>& result)
        -> asio::awaitable<bool>
    {
        constexpr auto op_base_token = select_ops_base_tokens<Ops...>[op_index];
        auto& op = std::get<op_index>(ops);

        auto const successful_alternative = co_await op.submit_if_ready();

        if (successful_alternative)
        {
            result.emplace(
                op.get_result(*successful_alternative),
                op_base_token + *successful_alternative);
            co_return true;
        }

        co_return false;
    }

    template <typename... Ops, std::size_t... indices>
    [[nodiscard]] auto select_waitfree_submit(
        std::tuple<Ops&...>& ops,
        std::optional<select_result<Ops...>>& result,
        std::index_sequence<indices...>)
        -> asio::awaitable<void>
    {
        ((co_await select_waitfree_submit_one<indices>(ops, result)) or ...);
    }
}  // namespace asiochan::detail