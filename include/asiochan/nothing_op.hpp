#pragma once

#include <compare>
#include <cstddef>
#include <optional>

#include "asiochan/asio.hpp"
#include "asiochan/channel_concepts.hpp"

namespace asiochan
{
    class no_result_t
    {
      public:
        [[nodiscard]] friend auto operator<=>(
            no_result_t const& lhs,
            no_result_t const& rhs) noexcept = default;

        [[nodiscard]] static auto matches(any_channel_type auto const&) noexcept -> bool
        {
            return false;
        }
    };

    inline constexpr auto no_result = no_result_t{};

    namespace ops
    {
        class nothing_t
        {
          public:
            using executor_type = asio::system_executor;
            using result_type = no_result_t;

            static constexpr auto num_alternatives = std::size_t{1};
            static constexpr auto always_waitfree = true;

            [[nodiscard]] static auto get_executor() -> executor_type
            {
                return asio::system_executor{};
            }

            [[nodiscard]] static auto submit_if_ready() noexcept -> std::optional<std::size_t>
            {
                return 0;
            }

            [[nodiscard]] static auto get_result(
                [[maybe_unused]] std::optional<std::size_t> successful_alternative) noexcept
                -> no_result_t
            {
                return no_result;
            }
        };

        inline constexpr auto nothing = nothing_t{};
    }  // namespace ops
}  // namespace asiochan
