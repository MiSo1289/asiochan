#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <queue>

#include "asiochan/asio.hpp"
#include "asiochan/channel_buff_size.hpp"
#include "asiochan/coro_promise.hpp"

namespace asiochan::detail
{
    template <sendable T>
    class channel_value_slot
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

        friend void transfer(channel_value_slot& from, channel_value_slot& to) noexcept
        {
            assert(from.value_.has_value());
            assert(not to.value_.has_value());
            to.value_.emplace(*std::move(from.value_));
        }

      private:
        std::optional<T> value_ = std::nullopt;
    };

    template <>
    class [[no_unique_address]] channel_value_slot<void>
    {
      public:
        friend void transfer(channel_value_slot&, channel_value_slot&) noexcept
        {
        }
    };

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

        void enqueue(channel_value_slot<T>& from) noexcept
        {
            assert(not full());
            transfer(from, buff_[(head_ + count_++) % size]);
        }

        void dequeue(channel_value_slot<T>& to) noexcept
        {
            assert(not empty());
            transfer(buff_[std::exchange(head_, (head_ + 1) % size)], to);
        }

      private:
        std::size_t head_ = 0;
        std::size_t count_ = 0;
        std::array<channel_value_slot<T>, size> buff_;
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
            if constexpr (size == unbounded_channel_buff)
            {
                return false;
            }
            else
            {
                return count_ == size;
            }
        }

        void enqueue(channel_value_slot<void>& from) noexcept
        {
            assert(not full());
            ++count_;
        }

        void dequeue(channel_value_slot<void>& to) noexcept
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

        [[noreturn]] void enqueue(channel_value_slot<void>& from) noexcept
        {
            std::terminate();
        }

        [[noreturn]] void dequeue(channel_value_slot<void>& to) noexcept
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

        void enqueue(channel_value_slot<T>& from) noexcept
        {
            queue_.push_back(from.read());
        }

        void dequeue(channel_value_slot<T>& to) noexcept
        {
            assert(not empty());
            to.write(std::move(queue.front()));
            queue.pop_front();
        }

      private:
        std::queue<T> queue_;
    };

    template <sendable T, channel_buff_size buff_size, asio::execution::executor Executor>
    class channel_shared_state
    {
      public:
        [[nodiscard]] explicit channel_shared_state(Executor const& executor)
          : strand_{executor} { }

        [[nodiscard]] auto try_read(channel_value_slot<T>& to) -> asio::awaitable<bool>
        {
            co_await asio::dispatch(strand_);

            auto const success = try_read_impl(to);

            co_await asio::post(co_await asio::this_coro::executor);
            co_return success;
        }

        [[nodiscard]] auto read(channel_value_slot<T>& to) -> asio::awaitable<void>
        {
            co_await asio::dispatch(strand_);

            if (not try_read_impl(to))
            {
                // Wait until a writer is ready.
                auto node = list_node{.slot = &to};
                enqueue_reader(node);
                co_await node.promise.get_awaitable();
            }

            co_await asio::post(co_await asio::this_coro::executor);
        }

        [[nodiscard]] auto try_write(channel_value_slot<T>& from) -> asio::awaitable<bool>
        {
            co_await asio::dispatch(strand_);

            auto const success = try_write_impl(from);

            co_await asio::post(co_await asio::this_coro::executor);
            co_return success;
        }

        [[nodiscard]] auto write(channel_value_slot<T>& from) -> asio::awaitable<void>
        {
            co_await asio::dispatch(strand_);

            if (not try_write_impl(from))
            {
                // Wait until a reader is ready
                auto node = list_node{.slot = &from};
                enqueue_writer(node);
                co_await node.promise.get_awaitable();
            }

            co_await asio::post(co_await asio::this_coro::executor);
        }

      private:
        struct list_node
        {
            coro_promise<void> promise;
            detail::channel_value_slot<T>* slot;
            list_node* next = nullptr;
        };

        asio::strand<Executor> strand_;
        list_node* reader_list_first_ = nullptr;
        list_node* reader_list_last_ = nullptr;
        list_node* writer_list_first_ = nullptr;
        list_node* writer_list_last_ = nullptr;
        channel_buffer<T, buff_size> buffer_;

        void enqueue_reader(list_node& node) noexcept
        {
            if (not reader_list_first_)
            {
                reader_list_first_ = &node;
                reader_list_last_ = &node;
            }
            else
            {
                reader_list_last_->next = &node;
                reader_list_last_ = &node;
            }
        }

        void enqueue_writer(list_node& node) noexcept
        {
            if (not writer_list_first_)
            {
                writer_list_first_ = &node;
                writer_list_last_ = &node;
            }
            else
            {
                writer_list_last_->next = &node;
                writer_list_last_ = &node;
            }
        }

        auto dequeue_reader() noexcept -> list_node*
        {
            if (reader_list_first_)
            {
                auto const node = reader_list_first_;
                reader_list_first_ = node->next;
                if (not node)
                {
                    reader_list_last_ = nullptr;
                }
                return node;
            }

            return nullptr;
        }

        auto dequeue_writer() noexcept -> list_node*
        {
            if (writer_list_first_)
            {
                auto const node = writer_list_first_;
                writer_list_first_ = node->next;
                if (not node)
                {
                    writer_list_last_ = nullptr;
                }
                return node;
            }

            return nullptr;
        }

        auto try_read_impl(channel_value_slot<T>& to) -> bool
        {
            if (not buffer_.empty())
            {
                // Get a value from the buffer.
                buffer_.dequeue(to);

                if (auto const writer = dequeue_writer())
                {
                    // Buffer was full with writers waiting.
                    // Wake the oldest writer and store his value in the buffer.
                    buffer_.enqueue(writer->slot);
                    writer->promise.set_value();
                }

                return true;
            }

            if (auto const writer = dequeue_writer())
            {
                // Get a value directly from a waiting writer.
                transfer(writer->slot, to);
                writer->promise.set_value();

                return true;
            }

            return false;
        }

        auto try_write_impl(channel_value_slot<T>& from) -> bool
        {
            if (auto const reader = dequeue_reader())
            {
                // Send the value directly to a waiting reader
                transfer(from, reader->slot);
                reader->promise.set_value();

                return true;
            }

            if (not buffer_.full())
            {
                // Store the value in the buffer.
                buffer_.enqueue(from);

                return true;
            }

            return false;
        }
    };
}  // namespace asiochan::detail
