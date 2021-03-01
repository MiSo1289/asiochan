#pragma once

#include <optional>
#include <utility>

#include "asiochan/asio.hpp"
#include "asiochan/channel_buff_size.hpp"
#include "asiochan/channel_concepts.hpp"
#include "asiochan/nothing_op.hpp"
#include "asiochan/read_op.hpp"
#include "asiochan/select.hpp"
#include "asiochan/sendable.hpp"
#include "asiochan/write_op.hpp"

namespace asiochan::detail
{
    template <sendable T,
              asio::execution::executor Executor,
              channel_buff_size buff_size,
              channel_flags flags,
              typename Derived>
    class channel_method_ops
    {
      public:
        // clang-format off
        [[nodiscard]] auto try_read() -> std::optional<T>
        requires (static_cast<bool>(flags & readable))
        // clang-format on
        {
            auto result = select_ready(
                ops::read(derived()),
                ops::nothing);

            if (auto const ptr = result.template get_if_received<T>())
            {
                return std::move(*ptr);
            }

            return std::nullopt;
        }

        // clang-format off
        [[nodiscard]] auto try_write(T value) -> bool
        requires (static_cast<bool>(flags & writable))
                 and (buff_size != unbounded_channel_buff)
        // clang-format on
        {
            auto const result = select_ready(
                ops::write(std::move(value), derived()),
                ops::nothing);

            return result.has_value();
        }

        // clang-format off
        [[nodiscard]] auto read() -> asio::awaitable<T, Executor>
        requires (static_cast<bool>(flags & readable))
        // clang-format on
        {
            auto result = co_await select(ops::read(derived()));

            co_return std::move(result).template get_received<T>();
        }

        // clang-format off
        [[nodiscard]] auto write(T value) -> asio::awaitable<void, Executor>
        requires (static_cast<bool>(flags & writable))
                 and (buff_size != unbounded_channel_buff)
        // clang-format on
        {
            co_await select(ops::write(std::move(value), derived()));
        }

        // clang-format off
        void write(T value)
        requires (static_cast<bool>(flags & writable))
                 and (buff_size == unbounded_channel_buff)
        // clang-format on
        {
            select_ready(ops::write(std::move(value), derived()));
        }

      private:
        [[nodiscard]] auto derived() noexcept -> Derived&
        {
            return static_cast<Derived&>(*this);
        }
    };

    template <channel_buff_size buff_size,
              asio::execution::executor Executor,
              channel_flags flags,
              typename Derived>
    class channel_method_ops<void, Executor, buff_size, flags, Derived>
    {
      public:
        // clang-format off
        [[nodiscard]] auto try_read() -> bool
        requires (static_cast<bool>(flags & readable))
        // clang-format on
        {
            auto const result = select_ready(
                ops::read(derived()),
                ops::nothing);

            return result.has_value();
        }

        // clang-format off
        [[nodiscard]] auto try_write() -> bool
        requires (static_cast<bool>(flags & writable))
                 and (buff_size != unbounded_channel_buff)
        // clang-format on
        {
            auto const result = select_ready(
                ops::write(derived()),
                ops::nothing);

            return result.has_value();
        }

        // clang-format off
        [[nodiscard]] auto read() -> asio::awaitable<void>
        requires (static_cast<bool>(flags & readable))
        // clang-format on
        {
            co_await select(ops::read(derived()));
        }

        // clang-format off
        [[nodiscard]] auto write() -> asio::awaitable<void>
        requires (static_cast<bool>(flags & writable))
                 and (buff_size != unbounded_channel_buff)
        // clang-format on
        {
            co_await select(ops::write(derived()));
        }

        // clang-format off
        void write()
        requires (static_cast<bool>(flags & writable))
                 and (buff_size == unbounded_channel_buff)
        // clang-format on
        {
            select_ready(ops::write(derived()));
        }

      private:
        [[nodiscard]] auto derived() noexcept -> Derived&
        {
            return static_cast<Derived&>(*this);
        }
    };
}  // namespace asiochan::detail
