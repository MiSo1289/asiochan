#pragma once

#include <concepts>
#include <cstddef>
#include <optional>
#include <type_traits>

#include "asiochan/asio.hpp"
#include "asiochan/detail/channel_waiter_list.hpp"
#include "asiochan/detail/type_traits.hpp"

namespace asiochan
{
    enum class select_op_submit_result
    {
        not_ready,
        ready,
    };

    // clang-format off
    template <typename T>
    concept select_op = requires (T& op)
    {
        typename T::result_type;
        typename std::integral_constant<std::size_t, T::num_alternatives>;
        typename std::bool_constant<T::always_waitfree>;

        { op.submit_if_ready() }
            -> std::same_as<asio::awaitable<std::optional<std::size_t>>>;

        { op.get_result() } -> std::same_as<typename T::result_type>;
    };

    template <typename T>
    concept waitfree_select_op = select_op<T> and T::always_waitfree;

    template <typename T>
    concept waitable_select_op
        = select_op<T>
          and not waitfree_select_op<T>
          and requires (
              T& op,
              detail::select_wait_context& select_ctx,
              detail::select_waiter_token const& base_token,
              std::optional<std::size_t> const& successful_alternative)
          {
              typename T::wait_state_type;
              requires std::default_initializable<typename T::wait_state_type>;

              requires requires (typename T::wait_state_type& wait_state)
              {
                  { op.submit_with_wait(select_ctx, base_token, wait_state) }
                      -> std::same_as<asio::awaitable<select_op_submit_result>>;

                  { op.clear_wait(successful_alternative, wait_state) }
                      -> std::same_as<asio::awaitable<void>>;
              };
          };

    template <typename... Ops>
    concept waitfree_selection
    = (sizeof...(Ops) >= 1u)
      and ((static_cast<std::size_t>(waitable_select_op<Ops>) + ...) == sizeof...(Ops) - 1u)
      and waitfree_select_op<detail::last_t<Ops...>>;

    template <typename... Ops>
    concept waitable_selection = (sizeof...(Ops) >= 1u) and (waitable_select_op<Ops> and ...);
    // clang-format on
}  // namespace asiochan
