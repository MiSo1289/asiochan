#pragma once

#include <memory>

#include "asiochan/asio.hpp"
#include "asiochan/channel_buff_size.hpp"
#include "asiochan/detail/channel_shared_state.hpp"
#include "asiochan/sendable.hpp"

namespace asiochan
{
    template <sendable T, channel_buff_size buff_size, asio::execution::executor Executor>
    class channel_base
    {
      protected:
        [[nodiscard]] explicit channel_base(Executor const& executor)
          : shared_state_{std::make_shared<state_type>(executor)} { }

        [[nodiscard]] auto try_read() -> asio::awaitable<std::optional<T>>
        {
            auto slot = detail::channel_value_slot<T>{};
            if (co_await shared_state_->try_read(slot))
            {
                co_return slot.read();
            }
            co_return std::nullopt;
        }

        [[nodiscard]] auto read() -> asio::awaitable<T>
        {
            auto slot = detail::channel_value_slot<T>{};
            co_await shared_state_->read(slot);
            co_return slot.read();
        }

        [[nodiscard]] auto try_write(T value) -> asio::awaitable<bool>
        {
            auto slot = detail::channel_value_slot<T>{};
            slot.write(std::move(value));
            co_return co_await shared_state_->try_write(slot);
        }

        [[nodiscard]] auto write(T value) -> asio::awaitable<void>
        {
            auto slot = detail::channel_value_slot<T>{};
            slot.write(std::move(value));
            co_await shared_state_->write(slot);
        }

      private:
        using state_type = detail::channel_shared_state<T, buff_size, Executor>;

        std::shared_ptr<state_type> shared_state_;
    };

    template <channel_buff_size buff_size, asio::execution::executor Executor>
    class channel_base<void, buff_size, Executor>
    {
      protected:
        [[nodiscard]] explicit channel_base(Executor const& executor)
          : shared_state_{std::make_shared<state_type>(executor)} { }

        [[nodiscard]] auto try_read() -> asio::awaitable<bool>
        {
            auto slot = detail::channel_value_slot<void>{};
            co_return co_await shared_state_->try_read(slot);
        }

        [[nodiscard]] auto read() -> asio::awaitable<void>
        {
            auto slot = detail::channel_value_slot<T>{};
            co_await shared_state_->read(slot);
        }

        [[nodiscard]] auto try_write() -> asio::awaitable<bool>
        {
            auto slot = detail::channel_value_slot<T>{};
            co_return co_await shared_state_->try_write(slot);
        }

        [[nodiscard]] auto write() -> asio::awaitable<void>
        {
            auto slot = detail::channel_value_slot<T>{};
            co_await shared_state_->write(slot);
        }

      private:
        using state_type = detail::channel_shared_state<void, buff_size, Executor>;

        std::shared_ptr<state_type> shared_state_;
    };

    template <sendable T, channel_buff_size buff_size, asio::execution::executor Executor>
    class basic_channel : public channel_base<T, buff_size, Executor>
    {
      private:
        using base = channel_base<T, buff_size, Executor>;

      public:
        using base::channel_base;

        basic_channel(base const& other)
          : base{other} { }

        basic_channel(base&& other)
          : base{std::move(other)} { }

        using base::try_read;

        using base::read;

        using base::try_write;

        using base::write;
    };

    template <sendable T, channel_buff_size buff_size, asio::execution::executor Executor>
    class basic_read_channel : public channel_base<T, buff_size, Executor>
    {
      private:
        using base = channel_base<T, buff_size, Executor>;

      public:
        using base::channel_base;

        basic_read_channel(base const& other)
          : base{other} { }

        basic_read_channel(base&& other)
          : base{std::move(other)} { }

        using base::try_read;

        using base::read;
    };

    template <sendable T, channel_buff_size buff_size, asio::execution::executor Executor>
    class basic_write_channel : public channel_base<T, buff_size, Executor>
    {
      private:
        using base = channel_base<T, buff_size, Executor>;

      public:
        using base::channel_base;

        basic_write_channel(base const& other)
          : base{other} { }

        basic_write_channel(base&& other)
          : base{std::move(other)} { }

        using base::try_write;

        using base::write;
    };

    template <sendable T, channel_buff_size buff_size = 0>
    using channel = basic_channel<T, buff_size, asio::any_io_executor>;

    template <sendable T, channel_buff_size buff_size = 0>
    using read_channel = basic_read_channel<T, buff_size, asio::any_io_executor>;

    template <sendable T, channel_buff_size buff_size = 0>
    using write_channel = basic_write_channel<T, buff_size, asio::any_io_executor>

        template <sendable T>
        using unbounded_channel = channel<T, unbounded_channel_buff>;

    template <sendable T>
    using unbounded_read_channel = read_channel<T, unbounded_channel_buff>;

    template <sendable T>
    using unbounded_write_channel = write_channel<T, unbounded_channel_buff>;
}  // namespace asiochan
