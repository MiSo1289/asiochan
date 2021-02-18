#pragma once

#include <concepts>

#include "asiochan/detail/channel_concepts.hpp"
#include "asiochan/detail/channel_shared_state.hpp"
#include "asiochan/detail/channel_waiter_list.hpp"
#include "asiochan/detail/send_slot.hpp"

// WIP
namespace asiochan
{
    enum class select_op_submit_result
    {
        not_ready,
        ready,
    };

    // clang-format off
    template <typename T>
    concept select_op = requires (T& op)
    {
        typename T::result_type;
        typename std::integral_constant<std::size_t, T::num_alternatives>;
        typename std::bool_constant<T::always_ready>;

        { op.submit_if_ready() }
            -> std::same_as<asio::awaitable<select_op_submit_result>>;

        { op.get_result() } -> std::same_as<typename T::result_type>;
    };

    template <typename T>
    concept waitfree_select_op = select_op<T> and T::always_ready;

    template <typename T>
    concept waitable_select_op
        = select_op<T>
            and not waitfree_select_op<T>
            and requires (
                T& op,
                detail::select_wait_context& select_ctx,
                detail::select_waiter_token const& base_token)
            {
                typename T::wait_state_type;
                requires std::default_initializable<typename T::wait_state_type>;

                requires requires (typename T::wait_state_type& wait_state)
                {
                    { op.submit_with_wait(select_ctx, base_token, wait_state) }
                        -> std::same_as<asio::awaitable<select_op_submit_result>>;
                }
            };
    // clang-format on

    template <sendable T>
    class read_result
    {
      public:
        template <std::convertible_to<T> U>
        explicit read_result(U&& value)
          : value_{std::forward<U>(value)}
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
    class read_result<void>
    {
    };

    template <sendable T>
    class write_result
    {
    };

    class no_result_t
    {
    };

    inline constexpr auto no_result = no_result_t{};

    namespace ops
    {
        template <sendable T, readable_channel_type<T> ChannelsHead, readable_channel_type<T>... ChannelsTail>
        class read
        {
          public:
            using result_type = read_result<T>;
            using slot_type = detail::send_slot<T>;
            using waiter_node_type = detail::channel_waiter_list_node<T>;

            struct wait_state_type
            {
                std::array<channel_waiter_list_node<T>, num_alternatives> waiter_nodes;
            };

            static constexpr auto num_alternatives = 1u + sizeof...(Channels);
            static constexpr auto always_ready = false;

            explicit read(ChannelsHead&, channels_head, ChannelsTail&... channels_tail) noexcept
              : channels_{channels_head, channels_tail...}
            {
            }

            read(read const&) = delete;
            read(read&&) = delete;

            [[nodiscard]] auto strand() -> typename ChannelsHead::shared_state_type::strand_type&
            {
                return std::get<0>(channels_).shared_state().strand();
            }

            [[nodiscard]] auto submit_if_ready() -> asio::awaitable<select_op_submit_result>
            {
                co_return co_await submit_impl(std::false_type{});
            }

            [[nodiscard]] auto submit_with_wait(
                detail::select_wait_context& select_ctx,
                detail::select_waiter_token const base_token,
                wait_state_type& wait_state)
                -> asio::awaitable<select_op_submit_result>
            {
                co_return co_await submit_impl(std::true_type{}, &select_ctx, base_token, &wait_state);
            }

            auto clear_wait(std::optional<std::size_t> successful_alternative)
                -> asio::awaitable<void>
            {
                co_await std::apply(
                    [&](auto&... chan_states) -> asio::awaitable<void> {
                        ((co_await [&](auto& chan_state) -> asio::awaitable<void> {
                             co_await asio::dispatch(chan_state.get_strand(), asio::use_awaitable);
                         }()),
                         ...);
                    },
                    chan_states_);
            }

            auto get_result() noexcept -> read_result<T>
            {
                return read_result<T>{slot_.extract()};
            }

          private:
            std::tuple<ChannelsHead&, ChannelsTail&...> channels_;
            slot_type slot_;

            template <bool enable_wait>
            [[nodiscard]] auto submit_impl(
                std::bool_constant<enable_wait>,
                detail::select_wait_context* const select_ctx = nullptr,
                std::optional<detail::select_waiter_token> const base_token = std::nullopt,
                wait_state_type* const wait_state = nullptr)
                -> asio::awaitable<select_op_submit_result>
            {
                co_return co_await([&]<std::size_t... indices>(std::index_sequence<indices...>)->asio::awaitable<select_op_submit_result> {
                    auto const is_ready
                        = ((co_await [&]<detail::channel_shared_state ChannelState>(ChannelState& channel_state) -> asio::awaitable<bool> {
                               constexpr auto channel_index = indices;

                               co_await asio::dispatch(channel_state.get_strand(), asio::use_awaitable);

                               if constexpr (ChannelState::buff_size != 0)
                               {
                                   if (not buffer_.empty())
                                   {
                                       // Get a value from the buffer.
                                       buffer_.dequeue(slot_);

                                       if constexpr (not ChannelState::write_never_waits)
                                       {
                                           if (auto const writer = channel_state.writer_list().dequeue_waiter())
                                           {
                                               // Buffer was full with writers waiting.
                                               // Wake the oldest writer and store his value in the buffer.
                                               buffer_.enqueue(*writer->slot);
                                               detail::notify_waiter(*writer);
                                           }
                                       }

                                       co_return true;
                                   }
                               }
                               else if (auto const writer = channel_state.writer_list().dequeue_waiter())
                               {
                                   // Get a value directly from a waiting writer.
                                   transfer(*writer->slot, slot_);

                                   co_return true;
                               }
                               else if constexpr (enable_wait)
                               {
                                   // Wait for a value.
                                   auto& waiter_node = wait_state->waiter_nodes[channel_index];
                                   waiter_node.select_wait_context = select_ctx;
                                   waiter_node.slot = &slot_;
                                   waiter_node.token = *base_token + channel_index;
                                   waiter_node.next = nullptr;

                                   channel_state.reader_list().enqueue_waiter(waiter_node);
                               }

                               co_return false;
                           }(std::get<indices>(channels).shared_state()))
                           or ...);

                    co_return is_ready
                        ? select_op_submit_result::completed
                        : select_op_submit_result::waiting;
                }(std::index_sequence_for<ChannelsHead, ChannelsTail...>{}));
            }
        };

        class nothing
        {
          public:
            using result_type = no_result_t;

            static constexpr auto num_alternatives = std::size_t{1};
            static constexpr auto always_ready = true;

            auto submit_if_ready(
                detail::select_wait_context& select_ctx,
                detail::select_waiter_token)
                -> asio::awaitable<select_op_submit_result>
            {
                co_return select_op_submit_result::ready;
            }
        };
    }  // namespace ops

    template <select_op OpsHead, select_op... OpsTail>
    auto select(OpsHead&& ops_head, OpsTail&&... ops_tail) -> asio::awaitable<void>
    {
        auto executor = co_await asio::this_coro::executor;
        auto strand = ops_head.get_strand();

        auto ops = std::tie(ops_head, ops_tail...);

        co_await asio::dispatch(ops_head.get_strand(), asio::use_awaitable);
        auto select_ctx = detail::select_wait_context{};

        constexpr auto ops_base_tokens = std::apply(
            []<select_op... Ops>(Ops & ... ops) {
                auto token_base = 0;
                return std::array{
                    std::exchange(token_base, token_base + Ops::num_alternatives)...,
                };
            },
            ops);

        asio::co_spawn(
            executor,
            ([&]<std::size_t... indices>(std::index_sequence<indices...>)->asio::awaitable<void> {
                co_await ops_head.submit(select_ctx, ops_base_tokens[indices]);
            }(std::index_sequence_for<OpsHead, OpsTail...>{})),
            asio::detached);

        co_await ctx.promise.get_awaitable();

        co_await([&]<std::size_t... indices>(std::index_sequence<indices...>)->asio::awaitable<void> {
            co_await ops_head.clear(select_ctx);
        }(std::index_sequence_for<OpsHead, OpsTail...>{}));
    }
}  // namespace asiochan
