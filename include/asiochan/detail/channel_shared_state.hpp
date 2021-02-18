#pragma once

#include "asiochan/asio.hpp"
#include "asiochan/detail/channel_buffer.hpp"
#include "asiochan/detail/channel_waiter_list.hpp"
#include "asiochan/detail/send_slot.hpp"

namespace asiochan::detail
{
    template <sendable T, bool enabled>
    class channel_shared_state_writer_list_base
    {
      public:
        using writer_list_type = channel_waiter_list<T>;

        static constexpr bool write_never_waits = false;

        [[nodiscard]] auto writer_list() noexcept -> writer_list_type&
        {
            return writer_list_;
        }

      protected:
        writer_list_type writer_list_;
    };

    template <sendable T>
    class channel_shared_state_writer_list_base<T, false>
    {
      public:
        using writer_list_type = void;

        static constexpr bool write_never_waits = true;
    };

    template <sendable T, channel_buff_size buff_size_, asio::execution::executor Executor>
    class channel_shared_state
      : private channel_shared_state_writer_list_base<T, buff_size_ != unbounded_channel_buff>
    {
      public:
        using strand_type = asio::strand<Executor>;
        using buffer_type = channel_buffer<T, buff_size_>;
        using node_type = channel_waiter_list_node<T>;
        using slot_type = send_slot<T>;
        using reader_list_type = channel_waiter_list<T>;

        static constexpr auto buff_size = buff_size_;

        [[nodiscard]] explicit channel_shared_state(Executor const& executor)
          : strand_{executor} { }

        [[nodiscard]] auto reader_list() noexcept -> reader_list_type&
        {
            return reader_list_;
        }

        [[nodiscard]] auto buffer() noexcept -> buffer_type&
        {
            return buffer_;
        }

        [[nodiscard]] auto strand() noexcept -> strand_type&
        {
            return strand_;
        }

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
                    if constexpr (buff_size_ != unbounded_channel_buff)
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
        reader_list_type reader_list_;
        [[no_unique_address]] buffer_type buffer_;

        auto try_read_impl(slot_type& to) -> bool
        {
            if constexpr (buff_size_ != 0)
            {
                if (not buffer_.empty())
                {
                    // Get a value from the buffer.
                    buffer_.dequeue(to);

                    if constexpr (buff_size_ != unbounded_channel_buff)
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

            if constexpr (buff_size_ != 0)
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

    template <typename T, typename SendType>
    struct is_channel_shared_state
      : std::false_type
    {
    };

    template <sendable SendType, channel_buff_size buff_size, asio::execution::executor Executor>
    struct is_channel_shared_state<
        channel_shared_state<SendType, buff_size, Executor>,
        SendType>
      : std::true_type
    {
    };

    template <typename T, sendable SendType>
    inline constexpr auto is_channel_shared_state_type_v
        = is_channel_shared_state<T, SendType>::value;

    template <typename T, typename SendType>
    concept channel_shared_state_type
        = is_channel_shared_state_type_v<T, SendType>;
}  // namespace asiochan::detail
