#pragma once

#include <array>
#include <cstddef>
#include <exception>
#include <optional>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <utility>

#include "asiochan/asio.hpp"
#include "asiochan/async_promise.hpp"
#include "asiochan/detail/channel_shared_state.hpp"
#include "asiochan/detail/channel_waiter_list.hpp"
#include "asiochan/detail/select_impl.hpp"
#include "asiochan/detail/send_slot.hpp"
#include "asiochan/select_concepts.hpp"
#include "asiochan/select_result.hpp"

namespace asiochan
{
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
            auto ops_wait_states = std::tuple<typename Ops::wait_state_type...>{};

            // Acquire the first operation's strand before awaiting on the promise
            co_await asio::dispatch(strand, asio::use_awaitable);

            asio::co_spawn(
                executor,
                detail::select_waitful_submit(
                    ops,
                    strand,
                    submit_completed,
                    submit_completion_promise,
                    ops_wait_states,
                    select_ctx,
                    std::index_sequence_for<Ops...>{}),
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
            co_await detail::select_waitfree_submit(
                ops,
                result,
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
}  // namespace asiochan
