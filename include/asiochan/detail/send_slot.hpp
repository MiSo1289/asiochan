#pragma once

#include "asiochan/sendable.hpp"

namespace asiochan::detail
{
    template <sendable T>
    class send_slot
    {
      public:
        auto read() noexcept -> T
        {
            assert(value_.has_value());
            return *std::exchange(value_, {});
        }

        void write(T&& value) noexcept
        {
            assert(not value_.has_value());
            value_.emplace(std::move(value));
        }

        friend void transfer(send_slot& from, send_slot& to) noexcept
        {
            assert(from.value_.has_value());
            assert(not to.value_.has_value());
            to.value_.emplace(*std::move(from.value_));
        }

      private:
        std::optional<T> value_ = std::nullopt;
    };

    template <>
    class send_slot<void>
    {
      public:
        friend void transfer(send_slot&, send_slot&) noexcept
        {
        }
    };
}  // namespace asiochan::detail
