#pragma once

#include <cassert>
#include <exception>
#include <functional>
#include <stdexcept>
#include <utility>

#include "asiochan/asio.hpp"
#include "asiochan/sendable.hpp"

namespace asiochan
{
    class broken_coro_promise : public std::exception
    {
      public:
        [[nodiscard]] constexpr auto what() const noexcept
            -> char const* override
        {
            return "Broken coro promise";
        }
    };

    template <typename T>
    class coro_promise;

    template <sendable_value T>
    class coro_promise<T> {
      public:
        coro_promise() noexcept = default;

        void set_value(T value)
        {
            assert(valid());
            std::exchange(impl_, {})(nullptr, std::move(value));
        }

        void set_exception(std::exception_ptr error)
        {
            assert(valid());
            std::exchange(impl_, {})(std::move(error), T{});
        }

        void reset()
        {
            if (valid())
            {
                set_exception(std::make_exception_ptr(broken_coro_promise{}));
            }
        }

        [[nodiscard]] auto valid() const noexcept -> bool
        {
            return not impl_.empty();
        }

        [[nodiscard]] auto get_awaitable() -> asio::awaitable<T>
        {
            assert(not valid());
            return asio::async_initiate<decltype(asio::use_awaitable), handler_sig>(
                [this](auto&& resumeCb) mutable {
                    impl_ = [resumeCb = std::forward<decltype(resumeCb)>(resumeCb)](
                                std::exception_ptr error, T result) mutable {
                        auto const executor = asio::get_associated_executor(resumeCb);
                        asio::post(
                            executor,
                            std::bind_front(
                                std::move(resumeCb), std::move(error), std::move(result)));
                    };
                },
                asio::use_awaitable);
        }

      private:
        using handler_sig = void(std::exception_ptr error, T value);
        using impl_type = std::function<handler_sig>;

        impl_type impl_;
    };

    template <>
    class coro_promise<void>
    {
      public:
        coro_promise() noexcept = default;

        void set_value()
        {
            assert(valid());
            std::exchange(impl_, {})(nullptr, std::move(value));
        }

        void set_exception(std::exception_ptr error)
        {
            assert(valid());
            std::exchange(impl_, {})(std::move(error), T{});
        }

        void reset()
        {
            if (valid())
            {
                set_exception(std::make_exception_ptr(broken_coro_promise{}));
            }
        }

        [[nodiscard]] auto valid() const noexcept -> bool
        {
            return not impl_.empty();
        }

        [[nodiscard]] auto get_awaitable() -> asio::awaitable<T>
        {
            assert(not valid());
            return asio::async_initiate<decltype(asio::use_awaitable), handler_sig>(
                [this](auto&& resumeCb) mutable {
                    impl_ = [resumeCb = std::forward<decltype(resumeCb)>(resumeCb)](
                                std::exception_ptr error) mutable {
                        auto const executor = asio::get_associated_executor(resumeCb);
                        asio::post(
                            executor,
                            std::bind_front(std::move(resumeCb), std::move(error)));
                    };
                },
                asio::use_awaitable);
        }

      private:
        using handler_sig = void(std::exception_ptr error);
        using impl_type = std::function<handler_sig>;

        impl_type impl_;
    };
}  // namespace asiochan
