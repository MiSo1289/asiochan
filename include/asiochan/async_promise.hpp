#pragma once

#include <cassert>
#include <concepts>
#include <exception>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

#include "asiochan/asio.hpp"
#include "asiochan/sendable.hpp"

namespace asiochan
{
    enum class async_promise_errc
    {
        broken_promise = 1,
    };

    [[nodiscard]] inline auto make_error_code(async_promise_errc const errc) noexcept -> system::error_code
    {
        class async_promise_category final : public system::error_category
        {
          public:
            [[nodiscard]] auto name() const noexcept -> char const* override
            {
                return "awaitable promise";
            }

            [[nodiscard]] auto message(int const errc) const -> std::string override
            {
                switch (static_cast<async_promise_errc>(errc))
                {
                case async_promise_errc::broken_promise:
                    return "broken promise";
                default:
                    return "unknown";
                }
            }
        };

        static constinit auto category = async_promise_category{};
        return system::error_code{static_cast<int>(errc), category};
    }
}  // namespace asiochan

template <>
struct asiochan::system::is_error_code_enum<asiochan::async_promise_errc>
  : std::true_type
{
};

namespace asiochan
{
    namespace detail
    {
        template <sendable T, asio::execution::executor Executor>
        struct async_promise_traits
        {
            using handler_sig = void(std::exception_ptr error, T value);
            using impl_type = std::optional<
                asio::detail::awaitable_handler<Executor, std::exception_ptr, T>>;
        };

        template <asio::execution::executor Executor>
        struct async_promise_traits<void, Executor>
        {
            using handler_sig = void(std::exception_ptr error);
            using impl_type = std::optional < asio::detail::awaitable_handler<Executor, std::exception_ptr>;
        };
    }  // namespace detail

    template <sendable T, asio::execution::executor Executor = asio::any_io_executor>
    class async_promise
    {
      public:
        async_promise() noexcept = default;

        template <std::convertible_to<T> U>
        void set_value(U&& value)
        {
            assert(valid());
            auto executor = asio::get_associated_executor(*impl_);
            asio::post(
                std::move(executor),
                std::bind_front(std::move(*impl_), nullptr, T{std::forward<U>(value)}));
            impl_.reset();
        }

        void set_value() requires std::is_void_v<T>
        {
            assert(valid());
            auto executor = asio::get_associated_executor(*impl_);
            asio::post(
                std::move(executor),
                std::bind_front(std::move(*impl_), nullptr));
            impl_.reset();
        }

        void set_exception(std::exception_ptr error) requires std::default_initializable<T>
        {
            assert(valid());
            auto executor = asio::get_associated_executor(*impl_);

            asio::post(
                std::move(executor),
                std::bind_front(std::move(*impl_), std::move(error), T{}));

            impl_.reset();
        }

        void set_exception(std::exception_ptr error) requires std::is_void_v<T>
        {
            assert(valid());
            auto executor = asio::get_associated_executor(*impl_);

            asio::post(
                std::move(executor),
                std::bind_front(std::move(*impl_), std::move(error)));

            impl_.reset();
        }

        // clang-format off
        void set_error_code(system::error_code const error)
            requires std::is_void_v<T> or std::default_initializable<T>
        // clang-format on
        {
            set_exception(std::make_exception_ptr(system::system_error{error}));
        }

        void reset()
        {
            if (valid())
            {
                set_error_code(async_promise_errc::broken_promise);
            }
        }

        [[nodiscard]] auto valid() const noexcept -> bool
        {
            return impl_.has_value();
        }

        [[nodiscard]] auto get_awaitable()
            -> asio::awaitable<T, Executor>
        {
            return get_awaitable([]() {});
        }

        // clang-format off
        template <std::invocable Continuation>
        requires std::movable<Continuation>
        [[nodiscard]] auto get_awaitable(Continuation continuation)
            -> asio::awaitable<T, Executor>
        // clang-format on
        {
            assert(not valid());
            return asio::async_initiate<
                asio::use_awaitable_t<Executor> const,
                typename traits_type::handler_sig>(
                [this, continuation = std::move(continuation)](auto&& resumeCb) mutable {
                    impl_.emplace(std::move(resumeCb));
                    std::invoke(std::move(continuation));
                },
                asio::use_awaitable_t<Executor>{});
        }

      private:
        using traits_type = detail::async_promise_traits<T, Executor>;

        typename traits_type::impl_type impl_;
    };

    template <sendable T = void,
              asio::execution::executor Executor = asio::any_io_executor>
    [[nodiscard]] auto suspend_coro(
        std::invocable<async_promise<T, Executor>&&> auto promise_acceptor)
        -> asio::awaitable<T, Executor>
    {
        auto promise = async_promise<T, Executor>{};
        co_return co_await promise.get_awaitable([&]() {
            std::invoke(std::move(promise_acceptor), std::move(promise));
        });
    }
}  // namespace asiochan
