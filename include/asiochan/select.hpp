#pragma once

#include <array>
#include <cstddef>
#include <exception>
#include <mutex>
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
    template <select_op... Ops,
              asio::execution::executor Executor = typename detail::head_t<Ops...>::executor_type>
    requires waitable_selection<Ops...>
    [[nodiscard]] auto select(Ops... ops_args)
        -> asio::awaitable<select_result<Ops...>, Executor>
    // clang-format on
    {
        auto result = std::optional<select_result<Ops...>>{};
        auto submit_mutex = std::mutex{};
        auto wait_ctx = detail::select_wait_context<Executor>{};
        auto ops_wait_states = std::tuple<typename Ops::wait_state_type...>{};

        auto const success_token = co_await suspend_with_promise<detail::select_waiter_token, Executor>(
            [](async_promise<detail::select_waiter_token, Executor>&& promise,
               auto* const submit_mutex,
               auto* const wait_ctx,
               auto* const ops_wait_states,
               auto* const... ops_args) {
                wait_ctx->promise = std::move(promise);

                auto ready_token = std::optional<std::size_t>{};

                {
                    auto const submit_lock = std::scoped_lock{*submit_mutex};

                    ([&]<std::size_t... indices>(std::index_sequence<indices...>) {
                        ([&]<std::size_t channel_index>(auto& op, detail::constant<channel_index>) {
                            constexpr auto op_base_token = detail::select_ops_base_tokens<Ops...>[channel_index];

                            if (auto const ready_alternative = op.submit_with_wait(
                                    *wait_ctx,
                                    op_base_token,
                                    std::get<channel_index>(*ops_wait_states)))
                            {
                                ready_token = op_base_token + *ready_alternative;

                                return true;
                            }

                            return false;
                        }(*ops_args, detail::constant<indices>{})
                         or ...);
                    }(std::index_sequence_for<Ops...>{}));
                }

                if (ready_token)
                {
                    wait_ctx->promise.set_value(*ready_token);
                }
            },
            &submit_mutex,
            &wait_ctx,
            &ops_wait_states,
            &ops_args...);

        auto const submit_lock = std::scoped_lock{submit_mutex};

        ([&]<std::size_t... indices>(std::index_sequence<indices...>) {
            ([&]<select_op Op, std::size_t channel_index>(Op& op, detail::constant<channel_index>) {
                constexpr auto op_base_token = detail::select_ops_base_tokens<Ops...>[channel_index];

                auto successful_alternative = std::optional<std::size_t>{};

                if (success_token >= op_base_token
                    and success_token < op_base_token + Op::num_alternatives)
                {
                    successful_alternative = success_token - op_base_token;
                    result.emplace(op.get_result(*successful_alternative), success_token);
                }

                op.clear_wait(
                    successful_alternative,
                    std::get<channel_index>(ops_wait_states));
            }(ops_args, detail::constant<indices>{}),
             ...);
        }(std::index_sequence_for<Ops...>{}));

        assert(result.has_value());

        co_return std::move(*result);
    }

    // clang-format off
    template <select_op... Ops>
    requires waitfree_selection<Ops...>
    auto select_ready(Ops... ops_args) -> select_result<Ops...>
    // clang-format on
    {
        auto result = std::optional<select_result<Ops...>>{};

        ([&]<std::size_t... indices>(std::index_sequence<indices...>) {
            ([&]<std::size_t channel_index>(auto& op, detail::constant<channel_index>) {
                constexpr auto op_base_token = detail::select_ops_base_tokens<Ops...>[channel_index];

                if (auto const ready_alternative = op.submit_if_ready())
                {
                    result.emplace(
                        op.get_result(*ready_alternative),
                        op_base_token + *ready_alternative);

                    return true;
                }

                return false;
            }(ops_args, detail::constant<indices>{})
             or ...);
        }(std::index_sequence_for<Ops...>{}));

        assert(result.has_value());

        return std::move(*result);
    }
}  // namespace asiochan
