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
    class read_result : public detail::channel_op_result_base<T>
    {
      private:
        using base = read_result::channel_op_result_base;

      public:
        template <std::convertible_to<T> U>
        read_result(U&& value, channel_type<T> auto& channel)
          : base{channel}
          , value_{std::forward<U>(value)}
        {
        }

        [[nodiscard]] auto get() & noexcept -> T&
        {
            return value_;
        }

        [[nodiscard]] auto get() const& noexcept -> T const&
        {
            return value_;
        }

        [[nodiscard]] auto get() && noexcept -> T&&
        {
            return std::move(value_);
        }

        [[nodiscard]] auto get() const&& noexcept -> T const&&
        {
            return std::move(value_);
        }

      private:
        T value_;
    };

    template <>
    class read_result<void> : public detail::channel_op_result_base<void>
    {
      private:
        using base = read_result::channel_op_result_base;

      public:
        using base::base;

        static void get() noexcept { }
    };

    namespace ops
    {
        template <sendable T, readable_channel_type<T> ChannelsHead, readable_channel_type<T>... ChannelsTail>
        class read
        {
          public:
            using result_type = read_result<T>;
            using slot_type = detail::send_slot<T>;
            using waiter_node_type = detail::channel_waiter_list_node<T>;

            static constexpr auto num_alternatives = 1u + sizeof...(ChannelsTail);
            static constexpr auto always_waitfree = false;

            struct wait_state_type
            {
                std::array<std::optional<detail::channel_waiter_list_node<T>>, num_alternatives> waiter_nodes = {};
            };

            explicit read(ChannelsHead& channels_head, ChannelsTail&... channels_tail) noexcept
              : channels_{channels_head, channels_tail...}
            {
            }

            [[nodiscard]] auto strand() -> typename ChannelsHead::shared_state_type::strand_type&
            {
                return std::get<0>(channels_).shared_state().strand();
            }

            [[nodiscard]] auto submit_if_ready() -> asio::awaitable<std::optional<std::size_t>>
            {
                co_return co_await submit_if_ready_impl(
                    std::index_sequence_for<ChannelsHead, ChannelsTail...>{});
            }

            [[nodiscard]] auto submit_with_wait(
                detail::select_wait_context& select_ctx,
                detail::select_waiter_token const base_token,
                wait_state_type& wait_state)
                -> asio::awaitable<select_waitful_submit_result>
            {
                co_return co_await submit_with_wait_impl(
                    select_ctx,
                    base_token,
                    wait_state,
                    std::index_sequence_for<ChannelsHead, ChannelsTail...>{});
            }

            [[nodiscard]] auto clear_wait(
                std::optional<std::size_t> const successful_alternative,
                wait_state_type& wait_state)
                -> asio::awaitable<void>
            {
                co_await clear_wait_impl(
                    successful_alternative,
                    wait_state,
                    std::index_sequence_for<ChannelsHead, ChannelsTail...>{});
            }

            auto get_result(std::size_t const successful_alternative) noexcept -> result_type
            {
                auto result = std::optional<result_type>{};

                ([&]<std::size_t... indices>(std::index_sequence<indices...>) {
                    ([&](auto& channel) {
                        constexpr auto channel_index = indices;

                        if (successful_alternative == channel_index)
                        {
                            if constexpr (std::is_void_v<T>)
                            {
                                result.emplace(channel);
                            }
                            else
                            {
                                result.emplace(slot_.read(), channel);
                            }
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

            template <std::size_t... indices>
            [[nodiscard]] auto submit_if_ready_impl(std::index_sequence<indices...>)
                -> asio::awaitable<std::optional<std::size_t>>
            {
                auto result = std::optional<std::size_t>{};
                ((co_await submit_if_ready_one<indices>(result)) or ...);
                co_return result;
            }

            template <std::size_t channel_index>
            [[nodiscard]] auto submit_if_ready_one(std::optional<std::size_t>& result)
                -> asio::awaitable<bool>
            {
                auto& channel_state = std::get<channel_index>(channels_).shared_state();
                using ChannelState = std::decay_t<decltype(channel_state)>;

                co_await asio::dispatch(channel_state.strand(), asio::use_awaitable);

                if constexpr (ChannelState::buff_size != 0)
                {
                    if (not channel_state.buffer().empty())
                    {
                        // Get a value from the buffer.
                        channel_state.buffer().dequeue(slot_);
                        result = channel_index;

                        if constexpr (not ChannelState::write_never_waits)
                        {
                            if (auto const writer = channel_state.writer_list().dequeue_first_available())
                            {
                                // Buffer was full with writers waiting.
                                // Wake the oldest writer and store his value in the buffer.
                                channel_state.buffer().enqueue(*writer->slot);
                                detail::notify_waiter(*writer);
                            }
                        }

                        co_return true;
                    }
                }
                else if (auto const writer = channel_state.writer_list().dequeue_first_available())
                {
                    // Get a value directly from a waiting writer.
                    transfer(*writer->slot, slot_);
                    detail::notify_waiter(*writer);
                    result = channel_index;

                    co_return true;
                }

                co_return false;
            }

            template <std::size_t... indices>
            [[nodiscard]] auto submit_with_wait_impl(
                detail::select_wait_context& select_ctx,
                detail::select_waiter_token const base_token,
                wait_state_type& wait_state,
                std::index_sequence<indices...>)
                -> asio::awaitable<select_waitful_submit_result>
            {
                auto const is_ready
                    = ((co_await submit_with_wait_one<indices>(
                           select_ctx,
                           base_token,
                           wait_state))
                       or ...);

                co_return is_ready
                    ? select_waitful_submit_result::completed_waitfree
                    : select_waitful_submit_result::waiting;
            }

            template <std::size_t channel_index>
            [[nodiscard]] auto submit_with_wait_one(
                detail::select_wait_context& select_ctx,
                detail::select_waiter_token const base_token,
                wait_state_type& wait_state)
                -> asio::awaitable<bool>
            {
                auto const token = base_token + channel_index;
                auto& channel_state = std::get<channel_index>(channels_).shared_state();
                using ChannelState = std::decay_t<decltype(channel_state)>;

                co_await asio::dispatch(channel_state.strand(), asio::use_awaitable);

                if constexpr (ChannelState::buff_size != 0)
                {
                    if (not channel_state.buffer().empty())
                    {
                        if (not claim(select_ctx))
                        {
                            // A different waiting operation succeeded concurrently
                            co_return true;
                        }

                        // Get a value from the buffer.
                        channel_state.buffer().dequeue(slot_);

                        if constexpr (not ChannelState::write_never_waits)
                        {
                            if (auto const writer = channel_state.writer_list().dequeue_first_available())
                            {
                                // Buffer was full with writers waiting.
                                // Wake the oldest writer and store his value in the buffer.
                                channel_state.buffer().enqueue(*writer->slot);
                                detail::notify_waiter(*writer);
                            }
                        }

                        select_ctx.promise.set_value(token);

                        co_return true;
                    }
                }
                else if (auto const writer = channel_state.writer_list().dequeue_first_available(select_ctx))
                {
                    // Get a value directly from a waiting writer.
                    transfer(*writer->slot, slot_);
                    detail::notify_waiter(*writer);
                    select_ctx.promise.set_value(token);

                    co_return true;
                }

                // Wait for a value.
                auto& waiter_node = wait_state.waiter_nodes[channel_index].emplace();
                waiter_node.ctx = &select_ctx;
                waiter_node.slot = &slot_;
                waiter_node.token = token;
                waiter_node.next = nullptr;

                channel_state.reader_list().enqueue(waiter_node);

                co_return false;
            }

            template <std::size_t... indices>
            [[nodiscard]] auto clear_wait_impl(
                std::optional<std::size_t> const successful_alternative,
                wait_state_type& wait_state,
                std::index_sequence<indices...>)
                -> asio::awaitable<void>
            {
                ((co_await clear_wait_one<indices>(successful_alternative, wait_state)), ...);
            }

            template <std::size_t channel_index>
            [[nodiscard]] auto clear_wait_one(
                std::optional<std::size_t> const successful_alternative,
                wait_state_type& wait_state)
                -> asio::awaitable<void>
            {
                auto& channel_state = std::get<channel_index>(channels_).shared_state();
                auto& waiter_node = wait_state.waiter_nodes[channel_index];

                if (channel_index == successful_alternative or not waiter_node.has_value())
                {
                    // No need to clear wait on a successful or unsubmitted sub-operation
                    co_return;
                }

                co_await asio::dispatch(channel_state.strand(), asio::use_awaitable);
                channel_state.reader_list().dequeue(*waiter_node);
            }
        };

        template <any_channel_type ChannelsHead, any_channel_type... ChannelsTail>
        read(ChannelsHead&, ChannelsTail&...) -> read<typename ChannelsHead::send_type, ChannelsHead, ChannelsTail...>;
    }  // namespace ops
}  // namespace asiochan
