#pragma once

#include <type_traits>

namespace asiochan::detail
{
    template <typename T, typename...>
    struct head : std::type_identity<T>
    {
    };

    template <typename... Ts>
    using head_t = typename head<Ts...>::type;

    template <typename T, typename... Ts>
    struct last : last<Ts...>
    {
    };

    template <typename T>
    struct last<T> : std::type_identity<T>
    {
    };

    template <typename... Ts>
    using last_t = typename last<Ts...>::type;

    template <auto value_>
    struct constant
    {
        static constexpr auto value = value_;
    };
}  // namespace asiochan::detail
