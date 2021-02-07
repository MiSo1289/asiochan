#pragma once

#include <concepts>
#include <type_traits>

namespace asiochan
{
    // clang-format off
    template <typename T>
    concept sendable_value = (not std::is_reference_v<T>)
        and std::is_nothrow_move_constructible_v<T>
        and std::is_nothrow_move_assignable_v<T>;

    template <typename T>
    concept sendable = sendable_value<T> or std::is_void_v<T>;
    // clang-format on
}  // namespace asiochan
