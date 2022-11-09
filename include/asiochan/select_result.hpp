#pragma once

#include <concepts>
#include <cstddef>
#include <stdexcept>
#include <utility>
#include <variant>

#include "asiochan/channel_concepts.hpp"
#include "asiochan/detail/overloaded.hpp"
#include "asiochan/nothing_op.hpp"
#include "asiochan/read_op.hpp"
#include "asiochan/select_concepts.hpp"
#include "asiochan/write_op.hpp"

namespace asiochan
{
    class bad_select_result_access final : public std::exception
    {
      public:
        [[nodiscard]] auto what() const noexcept -> char const* override
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
                    [](T const&)
                    { return true; },
                    [](auto const&)
                    { return false; },
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
            return std::visit(
                [&](auto const& result)
                { return result.matches(channel); },
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
                    [&](read_result<SendType> const& result)
                    { return result.matches(channel); },
                    [](auto const&)
                    { return false; },
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
                    [&](write_result<SendType> const& result)
                    { return result.matches(channel); },
                    [](auto const&)
                    { return false; },
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
                    [](T& result) -> T* { return &result; },
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
}  // namespace asiochan
