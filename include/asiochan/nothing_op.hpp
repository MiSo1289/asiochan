#pragma once

namespace asiochan
{
    class no_result_t
    {
      public:
        [[nodiscard]] static auto matches(channel_type_any auto const&) noexcept -> bool
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
            using result_type = no_result_t;

            static constexpr auto num_alternatives = std::size_t{1};
            static constexpr auto always_waitfree = true;

            static auto submit_if_ready() -> asio::awaitable<select_op_submit_result>
            {
                co_return select_op_submit_result::ready;
            }

            static auto get_result() noexcept -> no_result_t
            {
                return no_result;
            }
        };

        inline constexpr auto nothing = nothing_t{};
    }  // namespace ops
}  // namespace asiochan
