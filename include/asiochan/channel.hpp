#pragma once

#include <concepts>
#include <memory>

#include "asiochan/asio.hpp"
#include "asiochan/channel_buff_size.hpp"
#include "asiochan/channel_concepts.hpp"
#include "asiochan/detail/channel_method_ops.hpp"
#include "asiochan/detail/channel_shared_state.hpp"
#include "asiochan/sendable.hpp"

namespace asiochan
{
    template <sendable T,
              channel_buff_size buff_size,
              channel_flags flags_,
              asio::execution::executor Executor>
    class channel_base
    {
      public:
        using executor_type = Executor;
        using shared_state_type = detail::channel_shared_state<T, Executor, buff_size>;
        using send_type = T;

        static constexpr auto flags = flags_;

        // clang-format off
        template <typename ExecutorArg>
        requires asio::execution::executor<std::decay_t<ExecutorArg>>
                 and std::constructible_from<Executor, ExecutorArg&&>
        [[nodiscard]] explicit channel_base(ExecutorArg&& executor)
          : shared_state_{std::make_shared<shared_state_type>()}
          , executor_{std::forward<ExecutorArg>(executor)}
        // clang-format on
        {
        }

        // clang-format off
        template <std::derived_from<asio::execution_context> Ctx>
        requires requires (Ctx& ctx) {
            { ctx.get_executor() } -> asio::execution::executor;
            { ctx.get_executor() } -> std::convertible_to<Executor>;
        }
        // clang-format on
        [[nodiscard]] explicit channel_base(Ctx& ctx)
          : channel_base{ctx.get_executor()} { }

        // clang-format off
        template <channel_flags other_flags>
        requires ((other_flags & flags) == flags)
        [[nodiscard]] channel_base(
            channel_base<T, buff_size, other_flags, Executor> const& other)
          : shared_state_{other.shared_state_}
          , executor_{other.executor_}
        // clang-format on
        {
        }

        // clang-format off
        template <channel_flags other_flags>
        requires ((other_flags & flags) == flags)
        [[nodiscard]] channel_base(
            channel_base<T, buff_size, other_flags, Executor>&& other)
          : shared_state_{std::move(other.shared_state_)}
          , executor_{std::move(other.executor_)}
        // clang-format on
        {
        }

        [[nodiscard]] auto get_executor() const -> executor_type
        {
            return executor_;
        }

        [[nodiscard]] auto shared_state() noexcept -> shared_state_type&
        {
            return *shared_state_;
        }

        [[nodiscard]] friend auto operator==(
            channel_base const& lhs,
            channel_base const& rhs) noexcept -> bool
            = default;

      protected:
        ~channel_base() noexcept = default;

      private:
        template <sendable, channel_buff_size, channel_flags, asio::execution::executor>
        friend class channel_base;

        std::shared_ptr<shared_state_type> shared_state_;
        Executor executor_;
    };

    template <sendable T, channel_buff_size buff_size, asio::execution::executor Executor>
    class basic_channel
      : public channel_base<T, buff_size, bidirectional, Executor>,
        public detail::channel_method_ops<T, Executor, buff_size, bidirectional, basic_channel<T, buff_size, Executor>>
    {
      private:
        using base = basic_channel::channel_base;
        using ops = basic_channel::channel_method_ops;

      public:
        using base::base;

        using ops::try_read;

        using ops::read;

        using ops::try_write;

        using ops::write;
    };

    template <sendable T, channel_buff_size buff_size, asio::execution::executor Executor>
    class basic_read_channel
      : public channel_base<T, buff_size, readable, Executor>,
        public detail::channel_method_ops<T, Executor, buff_size, readable, basic_read_channel<T, buff_size, Executor>>
    {
      private:
        using base = basic_read_channel::channel_base;
        using ops = basic_read_channel::channel_method_ops;

      public:
        using base::base;

        using ops::try_read;

        using ops::read;
    };

    template <sendable T, channel_buff_size buff_size, asio::execution::executor Executor>
    class basic_write_channel
      : public channel_base<T, buff_size, writable, Executor>,
        public detail::channel_method_ops<T, Executor, buff_size, writable, basic_write_channel<T, buff_size, Executor>>
    {
      private:
        using base = basic_write_channel::channel_base;
        using ops = basic_write_channel::channel_method_ops;

      public:
        using base::base;

        using ops::try_write;

        using ops::write;
    };

    template <sendable T, channel_buff_size buff_size = 0>
    using channel = basic_channel<T, buff_size, asio::any_io_executor>;

    template <sendable T, channel_buff_size buff_size = 0>
    using read_channel = basic_read_channel<T, buff_size, asio::any_io_executor>;

    template <sendable T, channel_buff_size buff_size = 0>
    using write_channel = basic_write_channel<T, buff_size, asio::any_io_executor>;

    template <sendable T>
    using unbounded_channel = channel<T, unbounded_channel_buff>;

    template <sendable T>
    using unbounded_read_channel = read_channel<T, unbounded_channel_buff>;

    template <sendable T>
    using unbounded_write_channel = write_channel<T, unbounded_channel_buff>;
}  // namespace asiochan
