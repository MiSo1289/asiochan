#pragma once

#include <array>
#include <concepts>
#include <cstddef>
#include <exception>
#include <optional>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

#include "asiochan/asio.hpp"
#include "asiochan/async_promise.hpp"
#include "asiochan/channel_concepts.hpp"
#include "asiochan/detail/channel_shared_state.hpp"
#include "asiochan/detail/channel_waiter_list.hpp"
#include "asiochan/detail/overloaded.hpp"
#include "asiochan/detail/send_slot.hpp"
#include "asiochan/detail/type_traits.hpp"
#include "asiochan/nothing_op.hpp"
#include "asiochan/read_op.hpp"
#include "asiochan/select_concepts.hpp"
#include "asiochan/write_op.hpp"

namespace asiochan
{
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
        using discriminator_type = std::size_t;

        template <typename T>
        static constexpr auto is_alternative = (std::same_as<T, typename Ops::result_type> or ...);

        template <std::convertible_to<variant_type> T>
        select_result(T&& value, discriminator_type const alternative)
          : result_{std::forward<T>(value)}
          , alternative_{alternative}
        {
        }

        [[nodiscard]] friend auto operator==(
            select_result const& lhs,
            select_result const& rhs) noexcept -> bool
            = default;

        [[nodiscard]] friend auto operator<=>(
            select_result const& lhs,
            select_result const& rhs) noexcept = default;

        [[nodiscard]] auto to_variant() && noexcept -> variant_type
        {
            return std::move(result_);
        }

        [[nodiscard]] auto alternative() const noexcept -> discriminator_type
        {
            return alternative_;
        }

        // clang-format off
        template <typename T>
        requires is_alternative<T>
        [[nodiscard]] auto is() const noexcept -> bool
        // clang-format on
        {
            // std::holds_alternative does not support multiple type occurrence
            return std::visit(
                detail::overloaded{
                    [](T const&) { return true; },
                    [](auto const&) { return false; },
                },
                result_);
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
        [[nodiscard]] auto get() & -> T&
        // clang-format on
        {
            return std::visit(
                detail::overloaded{
                    [](T& result) -> T& { return result; },
                    [](auto const&) -> T& { throw bad_select_result_access{}; },
                },
                result_);
        }

        // clang-format off
        template <typename T>
        requires is_alternative<T>
        [[nodiscard]] auto get() const& -> T const&
        // clang-format on
        {
            return std::visit(
                detail::overloaded{
                    [](T const& result) -> T const& { return result; },
                    [](auto const&) -> T const& { throw bad_select_result_access{}; },
                },
                result_);
        }

        // clang-format off
        template <typename T>
        requires is_alternative<T>
        [[nodiscard]] auto get() && -> T&&
        // clang-format on
        {
            return std::visit(
                detail::overloaded{
                    [](T& result) -> T&& { return std::move(result); },
                    [](auto const&) -> T&& { throw bad_select_result_access{}; },
                },
                result_);
        }

        // clang-format off
        template <typename T>
        requires is_alternative<T>
        [[nodiscard]] auto get() const&& -> T const&&
        // clang-format on
        {
            return std::visit(
                detail::overloaded{
                    [](T const& result) -> T const&& { return std::move(result); },
                    [](auto const&) -> T const&& { throw bad_select_result_access{}; },
                },
                result_);
        }

        // clang-format off
        template <sendable_value T>
        requires is_alternative<read_result<T>>
        [[nodiscard]] auto get_received() & -> T&
        // clang-format on
        {
            return get<read_result<T>>().get();
        }

        // clang-format off
        template <sendable_value T>
        requires is_alternative<read_result<T>>
        [[nodiscard]] auto get_received() const& -> T const&
        // clang-format on
        {
            return get<read_result<T>>().get();
        }

        // clang-format off
        template <sendable_value T>
        requires is_alternative<read_result<T>>
        [[nodiscard]] auto get_received()&& -> T&&
        // clang-format on
        {
            return std::move(get<read_result<T>>().get());
        }

        // clang-format off
        template <sendable_value T>
        requires is_alternative<read_result<T>>
        [[nodiscard]] auto get_received() const&& -> T const&&
        // clang-format on
        {
            return std::move(get<read_result<T>>().get());
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
        template <typename T>
        requires is_alternative<T>
        [[nodiscard]] auto get_if() const noexcept -> T const*
        // clang-format on
        {
            return std::visit(
                detail::overloaded{
                    [](T const& result) -> T const* { return &result; },
                    [](auto const&) -> T const* { return nullptr; },
                },
                result_);
        }

        // clang-format off
        template <sendable_value T>
        requires is_alternative<read_result<T>>
        [[nodiscard]] auto get_if_received() noexcept -> T*
        // clang-format on
        {
            if (auto const result = get_if<read_result<T>>())
            {
                return &result->get();
            }
            return nullptr;
        }

        // clang-format off
        template <sendable_value T>
        requires is_alternative<read_result<T>>
        [[nodiscard]] auto get_if_received() const noexcept -> T const*
        // clang-format on
        {
            if (auto const result = get_if<read_result<T>>())
            {
                return &result->get();
            }
            return nullptr;
        }

        // clang-format off
        template <any_channel_type T>
        requires is_alternative<read_result<typename T::send_type>>
                 and sendable_value<typename T::send_type>
        [[nodiscard]] auto get_if_received_from(T const& channel) noexcept
            -> typename T::send_type*
        // clang-format on
        {
            if (auto const result = get_if<read_result<typename T::send_type>>();
                result and result->matches(channel))
            {
                return &result->get();
            }
            return nullptr;
        }

        // clang-format off
        template <any_channel_type T>
        requires is_alternative<read_result<typename T::send_type>>
                 and sendable_value<typename T::send_type>
        [[nodiscard]] auto get_if_received_from(T const& channel) const noexcept
            -> typename T::send_type const*
        // clang-format on
        {
            if (auto const result = get_if<read_result<typename T::send_type>>();
                result and result->matches(channel))
            {
                return &result->get();
            }
            return nullptr;
        }

      private:
        variant_type result_;
        discriminator_type alternative_;
    };

    // clang-format off
    template <select_op... Ops>
    requires waitable_selection<Ops...>
    [[nodiscard]] auto select(Ops&&... ops_args) -> asio::awaitable<select_result<Ops...>>
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
                      == select_op_submit_result::ready)
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
            co_await([&]<std::size_t... indices>(std::index_sequence<indices...>)->asio::awaitable<void> {
                ((co_await([&]<select_op Op>(Op& op) -> asio::awaitable<void> {
                     constexpr auto op_index = indices;
                     auto successful_alternative = std::optional<std::size_t>{};
                     if (successful_sub_op >= ops_base_tokens[op_index]
                         and successful_sub_op < ops_base_tokens[op_index] + Op::num_alternatives)
                     {
                         successful_alternative = successful_sub_op - ops_base_tokens[op_index];

                         // Set result
                         result.emplace(op.get_result(), successful_sub_op);
                     }

                     co_await op.clear_wait(successful_alternative, std::get<op_index>(ops_wait_states));
                 }(std::get<indices>(ops)))),
                 ...);
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

    // clang-format off
    template <select_op... Ops>
    requires waitfree_selection<Ops...>
    [[nodiscard]] auto select(Ops&&... ops_args) -> asio::awaitable<select_result<Ops...>>
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
