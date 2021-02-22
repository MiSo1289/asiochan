#pragma once

#include <type_traits>

namespace asiochan::detail
{
    template <typename T, typename... Ts>
    struct last : last<Ts...>
    {
    };

    template <typename T>
    struct last<T> : std::type_identity<T>
    {
    };

    template <typename T, typename... Ts>
    using last_t = typename last<T, Ts...>::type;
}  // namespace asiochan::detail
