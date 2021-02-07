#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <queue>

#include "asiochan/asio.hpp"
#include "asiochan/awaitable_promise.hpp"
#include "asiochan/channel_buff_size.hpp"

namespace asiochan::detail
{
    template <sendable T>
    class channel_slot
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

        friend void transfer(channel_slot& from, channel_slot& to) noexcept
        {
            assert(from.value_.has_value());
            assert(not to.value_.has_value());
            to.value_.emplace(*std::move(from.value_));
        }

      private:
        std::optional<T> value_ = std::nullopt;
    };

    template <>
    class channel_slot<void>
    {
      public:
        friend void transfer(channel_slot&, channel_slot&) noexcept
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

        [[nodiscard]] static constexpr auto can_fill() noexcept -> bool
        {
            return true;
        }

        void enqueue(channel_slot<T>& from) noexcept
        {
            assert(not full());
            transfer(from, buff_[(head_ + count_++) % size]);
        }

        void dequeue(channel_slot<T>& to) noexcept
        {
            assert(not empty());
            transfer(buff_[std::exchange(head_, (head_ + 1) % size)], to);
        }

      private:
        std::size_t head_ = 0;
        std::size_t count_ = 0;
        std::array<channel_slot<T>, size> buff_;
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
            if constexpr (can_fill())
            {
                return count_ == size;
            }
            else
            {
                return false;
            }
        }

        [[nodiscard]] static constexpr auto can_fill() noexcept -> bool
        {
            return size != unbounded_channel_buff;
        }

        void enqueue(channel_slot<void>& from) noexcept
        {
            assert(not full());
            ++count_;
        }

        void dequeue(channel_slot<void>& to) noexcept
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

        [[nodiscard]] static constexpr auto can_fill() noexcept -> bool
        {
            return true;
        }

        [[noreturn]] void enqueue(channel_slot<void>& from) noexcept
        {
            std::terminate();
        }

        [[noreturn]] void dequeue(channel_slot<void>& to) noexcept
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

        void enqueue(channel_slot<T>& from)
        {
            queue_.push(from.read());
        }

        void dequeue(channel_slot<T>& to) noexcept
        {
            assert(not empty());
            to.write(std::move(queue_.front()));
            queue_.pop();
        }

      private:
        std::queue<T> queue_;
    };

    template <sendable T>
    struct channel_waiter_list_node
    {
        awaitable_promise<void> promise;
        channel_slot<T>* slot;
        channel_waiter_list_node* next = nullptr;
    };

    template <sendable T>
    class channel_shared_state_reader_list_base
    {
      protected:
        void enqueue_reader(channel_waiter_list_node<T>& node) noexcept
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

        auto dequeue_reader() noexcept -> channel_waiter_list_node<T>*
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

      private:
        channel_waiter_list_node<T>* reader_list_first_ = nullptr;
        channel_waiter_list_node<T>* reader_list_last_ = nullptr;
    };

    template <sendable T, bool enabled>
    class channel_shared_state_writer_list_base
    {
      protected:
        void enqueue_writer(channel_waiter_list_node<T>& node) noexcept
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

        auto dequeue_writer() noexcept -> channel_waiter_list_node<T>*
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

      private:
        channel_waiter_list_node<T>* writer_list_first_ = nullptr;
        channel_waiter_list_node<T>* writer_list_last_ = nullptr;
    };

    template <sendable T>
    class channel_shared_state_writer_list_base<T, false>
    {
    };

    template <sendable T, channel_buff_size buff_size, asio::execution::executor Executor>
    class channel_shared_state
      : private channel_shared_state_reader_list_base<T>,
        private channel_shared_state_writer_list_base<T, buff_size != unbounded_channel_buff>
    {
      public:
        using strand_type = asio::strand<Executor>;
        using buffer_type = channel_buffer<T, buff_size>;
        using node_type = channel_waiter_list_node<T>;
        using slot_type = channel_slot<T>;

        [[nodiscard]] explicit channel_shared_state(Executor const& executor)
          : strand_{executor} { }

        [[nodiscard]] auto try_read(slot_type& to) -> asio::awaitable<bool>
        {
            co_await asio::dispatch(strand_, asio::use_awaitable);
            auto success = false;
            // Wrapped in optional, because the default constructor is expensive on MSVC
            auto exception = std::optional<std::exception_ptr>{};

            try
            {
                success = try_read_impl(to);
            }
            catch (...)
            {
                exception = std::current_exception();
            }

            co_await asio::post(co_await asio::this_coro::executor, asio::use_awaitable);
            if (exception)
            {
                std::rethrow_exception(std::move(*exception));
            }
            co_return success;
        }

        [[nodiscard]] auto read(slot_type& to) -> asio::awaitable<void>
        {
            co_await asio::dispatch(strand_, asio::use_awaitable);
            auto exception = std::optional<std::exception_ptr>{};

            try
            {
                if (not try_read_impl(to))
                {
                    // Wait until a writer is ready.
                    auto node = node_type{.slot = &to};
                    this->enqueue_reader(node);
                    co_await node.promise.get_awaitable();
                }
            }
            catch (...)
            {
                exception = std::current_exception();
            }

            co_await asio::post(co_await asio::this_coro::executor, asio::use_awaitable);
            if (exception)
            {
                std::rethrow_exception(std::move(*exception));
            }
        }

        [[nodiscard]] auto try_write(slot_type& from) -> asio::awaitable<bool>
        {
            co_await asio::dispatch(strand_, asio::use_awaitable);
            auto success = false;
            auto exception = std::optional<std::exception_ptr>{};

            try
            {
                success = try_write_impl(from);
            }
            catch (...)
            {
                exception = std::current_exception();
            }

            co_await asio::post(co_await asio::this_coro::executor, asio::use_awaitable);
            if (exception)
            {
                std::rethrow_exception(std::move(*exception));
            }
            co_return success;
        }

        [[nodiscard]] auto write(slot_type& from) -> asio::awaitable<void>
        {
            co_await asio::dispatch(strand_, asio::use_awaitable);
            auto exception = std::optional<std::exception_ptr>{};

            try
            {
                if (not try_write_impl(from))
                {
                    if constexpr (buff_size != unbounded_channel_buff)
                    {
                        // Wait until a reader is ready
                        auto node = node_type{.slot = &from};
                        this->enqueue_writer(node);
                        co_await node.promise.get_awaitable();
                    }
                    else
                    {
                        // With an unbounded buffer, the write always succeeds
                        // without waiting.
                    }
                }
            }
            catch (...)
            {
                exception = std::current_exception();
            }

            co_await asio::post(co_await asio::this_coro::executor, asio::use_awaitable);
            if (exception)
            {
                std::rethrow_exception(std::move(*exception));
            }
        }

      private:
        strand_type strand_;
        [[no_unique_address]] buffer_type buffer_;

        auto try_read_impl(slot_type& to) -> bool
        {
            if constexpr (buff_size != 0)
            {
                if (not buffer_.empty())
                {
                    // Get a value from the buffer.
                    buffer_.dequeue(to);

                    if constexpr (buff_size != unbounded_channel_buff)
                    {
                        if (auto const writer = this->dequeue_writer())
                        {
                            // Buffer was full with writers waiting.
                            // Wake the oldest writer and store his value in the buffer.
                            buffer_.enqueue(*writer->slot);
                            writer->promise.set_value();
                        }
                    }

                    return true;
                }
            }
            else if (auto const writer = this->dequeue_writer())
            {
                // Get a value directly from a waiting writer.
                transfer(*writer->slot, to);
                writer->promise.set_value();

                return true;
            }

            return false;
        }

        auto try_write_impl(slot_type& from) -> bool
        {
            if (auto const reader = this->dequeue_reader())
            {
                // Send the value directly to a waiting reader
                transfer(from, *reader->slot);
                reader->promise.set_value();

                return true;
            }

            if constexpr (buff_size != 0)
            {
                if (not buffer_.full())
                {
                    // Store the value in the buffer.
                    buffer_.enqueue(from);

                    return true;
                }
            }

            return false;
        }
    };
}  // namespace asiochan::detail
