#pragma once

#include "asiochan/channel_concepts.hpp"
#include "asiochan/sendable.hpp"

namespace asiochan::detail
{
    template <sendable T>
    class channel_op_result_base
    {
      public:
        explicit channel_op_result_base(channel_type<T> auto& channel)
          : shared_state_{&channel.shared_state()} { }

        [[nodiscard]] static auto matches(any_channel_type auto const&) noexcept -> bool
        {
            return false;
        }

        [[nodiscard]] auto matches(channel_type<T> auto const& channel) const noexcept -> bool
        {
            return &channel.shared_state() == shared_state_;
        }

      private:
        void* shared_state_ = nullptr;
    };
}  // namespace asiochan::detail
