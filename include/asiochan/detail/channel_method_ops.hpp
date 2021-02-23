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
              channel_buff_size buff_size,
              channel_flags flags,
              typename Derived>
    class channel_method_ops
    {
      public:
        // clang-format off
        [[nodiscard]] auto try_read() -> asio::awaitable<std::optional<T>>
        requires (static_cast<bool>(flags & readable))
        // clang-format on
        {
            auto result = co_await select(
                ops::read(derived()),
                ops::nothing);

            if (auto const ptr = result.template get_if_received<T>())
            {
                co_return std::move(*ptr);
            }

            co_return std::nullopt;
        }

        // clang-format off
        template <std::convertible_to<T> U>
        requires (static_cast<bool>(flags & writable))
                 and (buff_size != unbounded_channel_buff)
        [[nodiscard]] auto try_write(U&& value) -> asio::awaitable<bool>
        // clang-format on
        {
            auto const result = co_await select(
                ops::write(std::forward<U>(value), derived()),
                ops::nothing);

            co_return result.has_value();
        }

        // clang-format off
        [[nodiscard]] auto read() -> asio::awaitable<T>
        requires (static_cast<bool>(flags & readable))
        // clang-format on
        {
            auto result = co_await select(
                ops::read(derived()));

            co_return std::move(result).template get<T>();
        }

        // clang-format off
        template <std::convertible_to<T> U>
        requires (static_cast<bool>(flags & writable))
        [[nodiscard]] auto write(U&& value) -> asio::awaitable<void>
        // clang-format on
        {
            co_await select(
                ops::write(std::forward<U>(value), derived()));
        }

      private:
        [[nodiscard]] auto derived() noexcept -> Derived&
        {
            return static_cast<Derived&>(*this);
        }
    };

    template <channel_buff_size buff_size,
              channel_flags flags,
              typename Derived>
    class channel_method_ops<void, buff_size, flags, Derived>
    {
      public:
        // clang-format off
        [[nodiscard]] auto try_read() -> asio::awaitable<bool>
        requires (static_cast<bool>(flags & readable))
        // clang-format on
        {
            auto const result = co_await select(
                ops::read(derived()),
                ops::nothing);

            co_return result.has_value();
        }

        // clang-format off
        [[nodiscard]] auto try_write() -> asio::awaitable<bool>
        requires (static_cast<bool>(flags & writable))
                 and (buff_size != unbounded_channel_buff)
        // clang-format on
        {
            auto const result = co_await select(
                ops::write(derived()),
                ops::nothing);

            co_return result.has_value();
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
        // clang-format on
        {
            co_await select(ops::write(derived()));
        }

      private:
        [[nodiscard]] auto derived() noexcept -> Derived&
        {
            return static_cast<Derived&>(*this);
        }
    };
}  // namespace asiochan::detail
