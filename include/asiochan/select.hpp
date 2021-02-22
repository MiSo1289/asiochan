#pragma once

#include <concepts>

#include "asiochan/channel_concepts.hpp"
#include "asiochan/detail/channel_shared_state.hpp"
#include "asiochan/detail/channel_waiter_list.hpp"
#include "asiochan/detail/overloaded.hpp"
#include "asiochan/detail/send_slot.hpp"
#include "asiochan/detail/type_traits.hpp"
#include "asiochan/nothing_op.hpp"
#include "asiochan/read_op.hpp"
#include "asiochan/write_op.hpp"

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
        typename std::bool_constant<T::always_waitfree>;

        { op.submit_if_ready() }
            -> std::same_as<asio::awaitable<select_op_submit_result>>;

        { op.get_result() } -> std::same_as<typename T::result_type>;
    };

    template <typename T>
    concept waitfree_select_op = select_op<T> and T::always_waitfree;

    template <typename T>
    concept waitable_select_op
        = select_op<T>
            and not waitfree_select_op<T>
            and requires (
                T& op,
                detail::select_wait_context& select_ctx,
                detail::select_waiter_token const& base_token,
                std::optional<std::size_t> const& successful_alternative)
            {
                typename T::wait_state_type;
                requires std::default_initializable<typename T::wait_state_type>;

                requires requires (typename T::wait_state_type& wait_state)
                {
                    { op.submit_with_wait(select_ctx, base_token, wait_state) }
                        -> std::same_as<asio::awaitable<select_op_submit_result>>;

                    { op.clear_wait(successful_alternative, wait_state) }
                        -> std::same_as<asio::awaitable<void>>;
                }
            };

    template <typename OpsHead, typename... OpsTail>
    concept waitfree_selection
        = (static_cast<std::size_t>(waitable_select_op<OpsHead>)
              + (static_cast<std::size_t>(waitable_select_op<OpsTail>) + ...)
              == sizeof...(OpsTail))
          and waitfree_select_op<detail::last_t<OpsHead, OpsTail...>>;

    template <typename OpsHead, typename... OpsTail>
    concept waitable_selection
        = waitable_select_op<OpsHead>
          and (waitable_select_op<OpsTail> and ...);
    // clang-format on

    class bad_select_result_access : public std::exception
    {
      public:
        [[nodiscard]] virtual auto what() const noexcept -> char const*
        {
            return "bad select result access";
        }
    };

    template <select_op... Ops>
    class select_result
    {
      public:
        using variant_type = std::variant<typename Ops::result_type...>;

        template <typename T>
        static constexpr auto is_alternative = (std::same_as<T, typename Ops::result_type> or ...);

        template <std::convertible_to<variant_type> T>
        select_result(T&& value) : result_{std::forward<T>(value)} {}

        [[nodiscard]] auto to_variant() & -> variant_type&
        {
            return result_;
        }

        [[nodiscard]] auto to_variant() const& -> variant_type const&
        {
            return result_;
        }

        [[nodiscard]] auto to_variant() && -> variant_type&&
        {
            return std::move(result_);
        }

        [[nodiscard]] auto to_variant() const&& -> variant_type const&&
        {
            return std::move(result_);
        }

        // clang-format off
        template <typename T>
        requires is_alternative<T>
        [[nodiscard]] auto is() const noexcept -> bool
        // clang-format on
        {
            return std::visit(
                detail::overloaded{
                    [](T const&) { return true; },
                    [](auto const&) { return false; },
                },
                result_);
        }

        // clang-format off
        template <std::size_t index>
        requires (index < sizeof...(Ops))
        [[nodiscard]] auto is() const noexcept -> bool
        // clang-format on
        {
            return std::holds_alternative<index>(result_);
        }

        // clang-format off
        template <sendable T>
        requires is_alternative<read_result<T>>
        [[nodiscard]] auto received() const noexcept -> bool
        // clang-format on
        {
            return is<read_result<T>>();
        }

        // clang-format off
        template <sendable T>
        requires is_alternative<write_result<T>>
        [[nodiscard]] auto sent() const noexcept -> bool
        // clang-format on
        {
            return is<write_result<T>>();
        }

        // clang-format off
        [[nodiscard]] auto has_value() const noexcept -> bool
        requires is_alternative<no_result_t>
        // clang-format on
        {
            return not is<no_result_t>();
        }

        // clang-format off
        explicit operator bool() const noexcept
        requires is_alternative<no_result_t>
        // clang-format on
        {
            return has_value();
        }

        // clang-format off
        template <any_channel_type T>
        requires is_alternative<read_result<typename T::send_type>>
                 or is_alternative<write_result<typename T::send_type>>
        [[nodiscard]] auto matches(T const& channel) const noexcept -> bool
        // clang-format on
        {
            using SendType = typename T::send_type;

            return std::visit(
                [&](auto const& result) { return result.matches(channel); },
                result_);
        }

        // clang-format off
        template <any_readable_channel_type T>
        requires is_alternative<read_result<typename T::send_type>>
        [[nodiscard]] auto received_from(T const& channel) const noexcept -> bool
        // clang-format on
        {
            using SendType = typename T::send_type;

            return std::visit(
                detail::overloaded{
                    [&](read_result<SendType> const& result) { return result.matches(channel); },
                    [](auto const&) { return false; },
                },
                result_);
        }

        // clang-format off
        template <any_readable_channel_type T>
        requires is_alternative<write_result<typename T::send_type>>
        [[nodiscard]] auto sent_to(T const& channel) const noexcept -> bool
        // clang-format on
        {
            using SendType = typename T::send_type;

            return std::visit(
                detail::overloaded{
                    [&](write_result<SendType> const& result) { return result.matches(channel); },
                    [](auto const&) { return false; },
                },
                result_);
        }

        // clang-format off
        template <typename T>
        requires is_alternative<T>
        [[nodiscard]] auto get() -> T&
        // clang-format on
        {
            return std::visit(
                detail::overloaded{
                    [](T const& result) -> T& { return result; },
                    [](auto const&) -> T& { throw bad_select_result_access{}; },
                },
                result_);
        }

        // clang-format off
        template <typename T>
        requires is_alternative<read_result<T>>
        [[nodiscard]] auto get_received() -> T&
        // clang-format on
        {
            return get<read_result<T>>().get();
        }

        // clang-format off
        template <typename T>
        requires is_alternative<T>
        [[nodiscard]] auto get_if() noexcept -> T*
        // clang-format on
        {
            return std::visit(
                detail::overloaded{
                    [](T const& result) -> T* { return &result; },
                    [](auto const&) -> T* { return nullptr; },
                },
                result_);
        }

        // clang-format off
        template <any_channel_type T>
        requires is_alternative<typename T::send_type>
        [[nodiscard]] auto get_if_matches(T const& channel) noexcept -> typename T::send_type*
        // clang-format on
        {
            // TODO
        }

      private:
        variant_type result_;
    };

    // clang-format off
    template <select_op OpsHead, select_op... OpsTail>
    requires waitable_selection<OpsHead, OpsTail...>
    auto select(OpsHead&& ops_head, OpsTail&&... ops_tail)
        -> asio::awaitable<select_result<OpsHead, OpsTail...>>
    // clang-format on
    {
        // Prepare state
        auto executor = co_await asio::this_coro::executor;
        auto strand = ops_head.strand();
        auto ops = std::tie(ops_head, ops_tail...);
        auto select_ctx = detail::select_wait_context{};
        auto submit_completion_promise = awaitable_promise<void>{};
        auto submit_completed = false;
        constexpr auto ops_base_tokens = std::apply(
            []<select_op... Ops>(Ops & ... ops) {
                auto token_base = std::size_t{0};
                return std::array{
                    std::exchange(token_base, token_base + Ops::num_alternatives)...,
                };
            },
            ops);
        auto ops_wait_states = std::apply(
            []<select_op... Ops>(Ops & ... ops) {
                return std::tuple{
                    typename Ops::wait_state_type{}...,
                };
            },
            ops);

        co_await asio::dispatch(strand, asio::use_awaitable);

        asio::co_spawn(
            executor,
            ([&]<std::size_t... indices>(std::index_sequence<indices...>)->asio::awaitable<void> {
                // Submit operations
                (co_await std::get<indices>(ops)
                         .submit_with_wait(
                             select_ctx,
                             ops_base_tokens[indices],
                             std::get<indices>(ops_wait_states))
                     == select_op_submit_result::ready
                 or ...);

                // Synchronize with parent routine
                co_await asio::dispatch(strand, asio::use_awaitable);
                submit_completed = true;
                if (submit_completion_promise.valid())
                {
                    submit_completion_promise.set_value();
                }
            }(std::index_sequence_for<OpsHead, OpsTail...>{})),
            asio::detached);

        // Wait until one of the operations succeeds
        auto const successful_sub_op = co_await ctx.promise.get_awaitable();

        // Synchronize with submission routine
        co_await asio::dispatch(strand, asio::use_awaitable);
        if (not submit_completed)
        {
            co_await submit_completion_promise.get_awaitable();
        }

        // Clear remaining waits
        co_await([&]<std::size_t... indices>(std::index_sequence<indices...>)->asio::awaitable<void> {
            constexpr auto op_index = indices;
            (co_await([&]<select_op Op>(Op& op) -> asio::awaitable<void> {
                 auto successful_alternative = std::optional<std::size_t>{};
                 if (successful_subop >= ops_base_tokens[op_index]
                     and successful_subop < ops_base_tokens[op_index] + Op::num_alternatives)
                 {
                     successful_alternative = successful_subop - ops_base_tokens[op_index];
                 }

                 co_await op.clear_wait(successful_alternative, std::get<op_index>(ops_wait_states));
             }(std::get<op_index>(ops))),
             ...)
        }(std::index_sequence_for<OpsHead, OpsTail...>{}));
    }

    // clang-format off
    template <select_op OpsHead, select_op... OpsTail>
    requires waitfree_selection<OpsHead, OpsTail...>
    auto select(OpsHead&& ops_head, OpsTail&&... ops_tail) -> asio::awaitable<void>
    // clang-format on
    {
        // TODO
        auto executor = co_await asio::this_coro::executor;
        auto strand = ops_head.strand();
        auto ops = std::tie(ops_head, ops_tail...);

        co_await asio::dispatch(strand, asio::use_awaitable);

        asio::co_spawn(
            executor,
            ([&]<std::size_t... indices>(std::index_sequence<indices...>)->asio::awaitable<void> {
                // Submit operations
                (co_await([&]<select_op Op>(Op& op) -> asio::awaitable<bool> {
                     constexpr auto op_index = indices;
                     auto const op_submit_result = co_await op.submit_with_wait(
                         select_ctx,
                         ops_base_tokens[indices],
                         std::get<indices>(ops_wait_states));

                     return op_submit_result == select_op_submit_result::ready;
                 }(std::get<indices>(ops)))
                 or ...);

                // Synchronize with parent routine
                co_await asio::dispatch(strand, asio::use_awaitable);
                submit_completed = true;
                if (submit_completion_promise.valid())
                {
                    submit_completion_promise.set_value();
                }
            }(std::index_sequence_for<OpsHead, OpsTail...>{})),
            asio::detached);

        // Wait until one of the operations succeeds
        auto const successful_sub_op = co_await ctx.promise.get_awaitable();

        // Synchronize with submission routine
        co_await asio::dispatch(strand, asio::use_awaitable);
        if (not submit_completed)
        {
            co_await submit_completion_promise.get_awaitable();
        }

        // Clear remaining waits
        co_await([&]<std::size_t... indices>(std::index_sequence<indices...>)->asio::awaitable<void> {
            (co_await([&]<select_op Op>(Op& op) -> asio::awaitable<void> {
                 constexpr auto op_index = indices;

                 auto successful_alternative = std::optional<std::size_t>{};
                 if (successful_subop >= ops_base_tokens[op_index]
                     and successful_subop < ops_base_tokens[op_index] + Op::num_alternatives)
                 {
                     successful_alternative = successful_subop - ops_base_tokens[op_index];
                 }

                 co_await op.clear_wait(successful_alternative, std::get<op_index>(ops_wait_states));
             }(std::get<indices>(ops))),
             ...);
        }(std::index_sequence_for<OpsHead, OpsTail...>{}));
    }
}  // namespace asiochan
