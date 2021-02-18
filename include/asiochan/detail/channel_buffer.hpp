#pragma once

#include <array>
#include <cassert>
#include <cstddef>
#include <exception>
#include <queue>
#include <type_traits>

#include "asiochan/channel_buff_size.hpp"
#include "asiochan/detail/send_slot.hpp"
#include "asiochan/sendable.hpp"

namespace asiochan::detail
{
    template <sendable T, channel_buff_size size>
    class channel_buffer
    {
      public:
        [[nodiscard]] auto empty() const noexcept -> bool
        {
            return count_ == 0;
        }

        [[nodiscard]] auto full() const noexcept -> bool
        {
            return count_ == size;
        }

        void enqueue(send_slot<T>& from) noexcept
        {
            assert(not full());
            transfer(from, buff_[(head_ + count_++) % size]);
        }

        void dequeue(send_slot<T>& to) noexcept
        {
            assert(not empty());
            --count_;
            transfer(buff_[std::exchange(head_, (head_ + 1) % size)], to);
        }

      private:
        std::size_t head_ = 0;
        std::size_t count_ = 0;
        std::array<send_slot<T>, size> buff_;
    };

    // clang-format off
    template <channel_buff_size size>
    requires (size > 0)
    class channel_buffer<void, size>
    // clang-format on
    {
      public:
        [[nodiscard]] auto empty() const noexcept -> bool
        {
            return count_ == 0;
        }

        [[nodiscard]] auto full() const noexcept -> bool
        {
            if constexpr (size != unbounded_channel_buff)
            {
                return count_ == size;
            }
            else
            {
                return false;
            }
        }

        void enqueue(send_slot<void>& from) noexcept
        {
            assert(not full());
            ++count_;
        }

        void dequeue(send_slot<void>& to) noexcept
        {
            assert(not empty());
            --count_;
        }

      private:
        std::size_t count_ = 0;
    };

    template <sendable T>
    class channel_buffer<T, 0>
    {
      public:
        [[nodiscard]] auto empty() const noexcept -> bool
        {
            return true;
        }

        [[nodiscard]] auto full() const noexcept -> bool
        {
            return true;
        }

        [[noreturn]] void enqueue(send_slot<void>& from) noexcept
        {
            std::terminate();
        }

        [[noreturn]] void dequeue(send_slot<void>& to) noexcept
        {
            std::terminate();
        }
    };

    // clang-format off
    template <sendable T>
    requires (not std::is_void_v<T>)
    class channel_buffer<T, unbounded_channel_buff>
    // clang-format on
    {
      public:
        [[nodiscard]] auto empty() const noexcept -> bool
        {
            return queue_.empty();
        }

        [[nodiscard]] static auto full() noexcept -> bool
        {
            return false;
        }

        void enqueue(send_slot<T>& from)
        {
            queue_.push(from.read());
        }

        void dequeue(send_slot<T>& to) noexcept
        {
            assert(not empty());
            to.write(std::move(queue_.front()));
            queue_.pop();
        }

      private:
        std::queue<T> queue_;
    };
}  // namespace asiochan::detail
