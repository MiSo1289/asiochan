#pragma once

#include <cassert>
#include <exception>
#include <functional>
#include <optional>
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
    class coro_promise<T>
    {
      public:
        coro_promise() noexcept = default;

        void set_value(T const& value)
        {
            assert(valid());
            auto executor = asio::get_associated_executor(*impl_);
            asio::post(
                std::move(executor),
                std::bind_front(std::move(*impl_), nullptr, value));
            impl_.reset();
        }

        void set_value(T&& value)
        {
            assert(valid());
            auto executor = asio::get_associated_executor(*impl_);
            asio::post(
                std::move(executor),
                std::bind_front(std::move(*impl_), nullptr, std::move(value)));
            impl_.reset();
        }

        void set_exception(std::exception_ptr error)
        {
            assert(valid());
            auto executor = asio::get_associated_executor(*impl_);
            asio::post(
                std::move(executor),
                std::bind_front(std::move(*impl_), std::move(error), T{}));
            impl_.reset();
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
                    impl_.emplace(std::move(resumeCb));
                },
                asio::use_awaitable);
        }

      private:
        using handler_sig = void(std::exception_ptr error, T value);
        using impl_type = std::optional<
            boost::asio::detail::awaitable_handler<asio::any_io_executor, std::exception_ptr, T>>;

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
            auto executor = asio::get_associated_executor(*impl_);
            asio::post(
                std::move(executor),
                std::bind_front(std::move(*impl_), nullptr));
            impl_.reset();
        }

        void set_exception(std::exception_ptr error)
        {
            assert(valid());
            auto executor = asio::get_associated_executor(*impl_);
            asio::post(
                std::move(executor),
                std::bind_front(std::move(*impl_), std::move(error)));
            impl_.reset();
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
            return static_cast<bool>(impl_);
        }

        [[nodiscard]] auto get_awaitable() -> asio::awaitable<void>
        {
            assert(not valid());
            return asio::async_initiate<decltype(asio::use_awaitable), handler_sig>(
                [this](auto&& resumeCb) mutable {
                    impl_.emplace(std::move(resumeCb));
                },
                asio::use_awaitable);
        }

      private:
        using handler_sig = void(std::exception_ptr error);
        using impl_type = std::optional<
            boost::asio::detail::awaitable_handler<asio::any_io_executor, std::exception_ptr>>;

        impl_type impl_;
    };
}  // namespace asiochan
