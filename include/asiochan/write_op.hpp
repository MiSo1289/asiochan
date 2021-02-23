#pragma once

#include <array>
#include <concepts>
#include <cstddef>
#include <optional>
#include <type_traits>
#include <utility>

#include "asiochan/asio.hpp"
#include "asiochan/channel_concepts.hpp"
#include "asiochan/detail/channel_op_result_base.hpp"
#include "asiochan/detail/channel_waiter_list.hpp"
#include "asiochan/detail/send_slot.hpp"
#include "asiochan/select_concepts.hpp"
#include "asiochan/sendable.hpp"

namespace asiochan
{
    template <sendable T>
    class write_result : public detail::channel_op_result_base<T>
    {
      private:
        using base = write_result::channel_op_result_base;

      public:
        using base::base;
    };

    namespace ops
    {
        template <sendable T, writable_channel_type<T> ChannelsHead, readable_channel_type<T>... ChannelsTail>
        class write
        {
          private:
            static constexpr auto num_always_waitfree
                = static_cast<std::size_t>(ChannelsHead::shared_state_type::write_never_waits)
                  + (static_cast<std::size_t>(ChannelsTail::shared_state_type::write_never_waits) + ...);
            static constexpr auto last_always_waitfree
                = detail::last_t<ChannelsHead, ChannelsTail...>::write_never_waits;

            static_assert(
                num_always_waitfree == 0 or (num_always_waitfree == 1 and last_always_waitfree),
                "Only the last target channel of a write operation can be unbounded");

          public:
            using result_type = read_result<T>;
            using slot_type = detail::send_slot<T>;
            using waiter_node_type = detail::channel_waiter_list_node<T>;

            static constexpr auto num_alternatives = sizeof...(ChannelsTail) + 1u;
            static constexpr auto always_waitfree = last_always_waitfree;

            struct wait_state_type
            {
                std::array<std::optional<detail::channel_waiter_list_node<T>>, num_alternatives> waiter_nodes = {};
            };

            // clang-format off
            template <std::convertible_to<T> U>
            requires sendable_value<T>
            write(U&& value, ChannelsHead& channels_head, ChannelsTail&... channels_tail) noexcept
              // clang-format on
              : channels_{channels_head, channels_tail...}
            {
                slot_.write(std::forward<U>(value));
            }

            // clang-format off
            explicit write(ChannelsHead& channels_head, ChannelsTail&... channels_tail) noexcept
            requires std::is_void_v<T>
              // clang-format on
              : channels_{channels_head, channels_tail...}
            {
            }

            write(write const&) = delete;
            write(write&&) = delete;

            [[nodiscard]] auto strand() -> typename ChannelsHead::shared_state_type::strand_type&
            {
                return std::get<0>(channels_).shared_state().strand();
            }

            [[nodiscard]] auto submit_if_ready() -> asio::awaitable<std::optional<std::size_t>>
            {
                auto result = std::optional<std::size_t>{};

                co_await([&]<std::size_t... indices>(std::index_sequence<indices...>)->asio::awaitable<void> {
                    ((co_await [&]<typename ChannelState>(ChannelState& channel_state) -> asio::awaitable<bool> {
                         constexpr auto channel_index = indices;

                         co_await asio::dispatch(channel_state.strand(), asio::use_awaitable);

                         if (auto const reader = channel_state.reader_list().dequeue_first_available())
                         {
                             // Buffer was empty with readers waiting.
                             // Wake the oldest reader and give him a value.
                             transfer(slot_, *reader->slot);
                             detail::notify_waiter(*reader);

                             co_return true;
                         }
                         else if constexpr (ChannelState::buff_size != 0)
                         {
                             if (not channel_state.buffer().full())
                             {
                                 // Store the value in the buffer.
                                 channel_state.buffer().enqueue(slot_);
                                 result = channel_index;

                                 co_return true;
                             }
                         }

                         co_return false;
                     }(std::get<indices>(channels_).shared_state()))
                     or ...);
                }(std::index_sequence_for<ChannelsHead, ChannelsTail...>{}));

                co_return result;
            }

            // clang-format off
            [[nodiscard]] auto submit_with_wait(
                detail::select_wait_context& select_ctx,
                detail::select_waiter_token const base_token,
                wait_state_type& wait_state)
                -> asio::awaitable<select_op_submit_result>
            requires (not always_waitfree)
            // clang-format on
            {
                co_return co_await([&]<std::size_t... indices>(std::index_sequence<indices...>)->asio::awaitable<select_op_submit_result> {
                    auto const is_ready
                        = ((co_await [&]<typename ChannelState>(ChannelState& channel_state) -> asio::awaitable<bool> {
                               constexpr auto channel_index = indices;
                               constexpr auto token = base_token + channel_index;

                               co_await asio::dispatch(channel_state.strand(), asio::use_awaitable);

                               if (auto const reader = channel_state.reader_list().dequeue_first_available(select_ctx))
                               {
                                   // Buffer was empty with readers waiting.
                                   // Wake the oldest reader and give him a value.
                                   transfer(slot_, *reader->slot);
                                   detail::notify_waiter(*reader);
                                   select_ctx.promise.set_value(token);

                                   co_return true;
                               }
                               else if constexpr (ChannelState::buff_size != 0)
                               {
                                   if (not channel_state.buffer().full())
                                   {
                                       if (claim(select_ctx))
                                       {
                                           // Store the value in the buffer.
                                           channel_state.buffer().enqueue(slot_);
                                           select_ctx.promise.set_value(token);
                                       }

                                       co_return true;
                                   }
                               }

                               // Wait for a value.
                               auto& waiter_node = wait_state.waiter_nodes[channel_index].emplace();
                               waiter_node.select_wait_context = &select_ctx;
                               waiter_node.slot = &slot_;
                               waiter_node.token = token;
                               waiter_node.next = nullptr;

                               channel_state.writer_list().enqueue(waiter_node);

                               co_return false;
                           }(std::get<indices>(channels_).shared_state()))
                           or ...);

                    co_return is_ready
                        ? select_op_submit_result::ready
                        : select_op_submit_result::not_ready;
                }(std::index_sequence_for<ChannelsHead, ChannelsTail...>{}));
            }

            // clang-format off
            auto clear_wait(
                std::optional<std::size_t> const successful_alternative,
                wait_state_type& wait_state)
                -> asio::awaitable<void>
            requires (not always_waitfree)
            // clang-format on
            {
                co_await([&]<std::size_t... indices>(std::index_sequence<indices...>)->asio::awaitable<void> {
                    (co_await([&](auto& channel_state) -> asio::awaitable<void> {
                         constexpr auto channel_index = indices;

                         auto& waiter_node = wait_state.waiter_nodes[channel_index];

                         if (channel_index == successful_alternative or not waiter_node.has_value())
                         {
                             // No need to clear wait on a successful or unsubmitted sub-operation
                             co_return;
                         }

                         co_await asio::dispatch(channel_state.strand(), asio::use_awaitable);
                         channel_state.writer_list().dequeue(*waiter_node);
                     }(std::get<indices>(channels_).shared_state())),
                     ...);
                }(std::index_sequence_for<ChannelsHead, ChannelsTail...>{}));
            }

            auto get_result(std::size_t const successful_alternative) noexcept -> result_type
            {
                auto result = std::optional<result_type>{};

                ([&]<std::size_t... indices>(std::index_sequence<indices...>) {
                    ([&](auto& channel) {
                        constexpr auto channel_index = indices;

                        if (successful_alternative == channel_index)
                        {
                            result.emplace(channel);
                            return true;
                        }

                        return false;
                    }(std::get<indices>(channels_))
                     or ...);
                }(std::index_sequence_for<ChannelsHead, ChannelsTail...>{}));

                assert(result.has_value());

                return std::move(*result);
            }

          private:
            std::tuple<ChannelsHead&, ChannelsTail&...> channels_;
            [[no_unique_address]] slot_type slot_;
        };

        template <typename U, any_channel_type ChannelsHead, any_channel_type... ChannelsTail>
        write(U&&, ChannelsHead&, ChannelsTail&...) -> write<typename ChannelsHead::send_type, ChannelsHead, ChannelsTail...>;
    }  // namespace ops
}  // namespace asiochan
