#pragma once

#include <atomic>
#include <exception>
#include <mutex>
#include <optional>

#include "asiochan/asio.hpp"
#include "asiochan/awaitable_promise.hpp"

namespace asiochan
{
    template <sendable T, typename Mutex = std::mutex>
    class synchronized_awaitable_promise;

    template <sendable_value T, typename Mutex>
    class synchronized_awaitable_promise<T, Mutex>
    {
      public:
        template <std::convertible_to<T> U>
        void set_value(U&& value)
        {
            auto lock = std::scoped_lock{mutex_};
            if (promise_.valid())
            {
                promise_.set_value(std::forward<U>(value));
            }
            else
            {
                if (ready_value_ or ready_exception_)
                {
                    throw system::system_error{awaitable_promise_errc::result_already_set};
                }

                ready_value_.emplace(std::forward<U>(value));
            }
        }

        void set_exception(std::exception_ptr error)
        {
            auto lock = std::scoped_lock{mutex_};
            if (promise_.valid())
            {
                promise_.set_exception(std::move(error));
            }
            else
            {
                if (ready_value_ or ready_exception_)
                {
                    throw system::system_error{awaitable_promise_errc::result_already_set};
                }

                ready_exception_.emplace(std::move(error));
            }
        }

        void set_error_code(system::error_code const error)
        {
            set_exception(std::make_exception_ptr(system::system_error{error}));
        }

        void set_error_code(system::error_code const error)
        {
            set_exception(std::make_exception_ptr(system::system_error{error}));
        }

      private:
        awaitable_promise<T> promise_;
        std::optional<T> ready_value_ = std::nullopt;
        std::optional<std::exception_ptr> ready_exception_ = std::nullopt;
        Mutex mutex_;
    };

    template <typename Mutex>
    class synchronized_awaitable_promise<void, Mutex>
    {
      public:
        void set_value()
        {
            auto lock = std::scoped_lock{mutex_};
            if (promise_.valid())
            {
                promise_.set_value();
            }
            else
            {
                if (ready_value_ or ready_exception_)
                {
                    throw system::system_error{awaitable_promise_errc::result_already_set};
                }

                ready_value_ = true;
            }
        }

        void set_exception(std::exception_ptr error)
        {
            auto lock = std::scoped_lock{mutex_};
            if (promise_.valid())
            {
                promise_.set_exception(std::move(error));
            }
            else
            {
                if (ready_value_ or ready_exception_)
                {
                    throw system::system_error{awaitable_promise_errc::result_already_set};
                }

                ready_exception_.emplace(std::move(error));
            }
        }

        void set_error_code(system::error_code const error)
        {
            set_exception(std::make_exception_ptr(system::system_error{error}));
        }

      private:
        awaitable_promise<T> promise_;
        bool ready_value_ = false;
        std::optional<std::exception_ptr> ready_exception_ = std::nullopt;
        Mutex mutex_;
    };
}  // namespace asiochan
