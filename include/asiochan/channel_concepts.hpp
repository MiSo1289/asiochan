#pragma once

#include "asiochan/detail/channel_shared_state.hpp"

namespace asiochan
{
    enum channel_flags : unsigned
    {
        readable = 1u << 0u,
        writable = 1u << 1u,
        bidirectional = readable | writable,
    };

    // clang-format off
    template <typename T>
    concept any_channel_type = requires (T& channel)
    {
        typename T::shared_state_type;

        { channel.shared_state() } noexcept -> std::same_as<typename T::shared_state_type&>;
    };

    template <typename T, typename SendType>
    concept channel_type
        = any_channel_type<T>
          and detail::channel_shared_state_type<typename T::shared_state_type, SendType>;

    template <typename T, typename SendType>
    concept readable_channel_type
        = channel_type<T, SendType> and (T::flags & channel_flags::readable);

    template <typename T, typename SendType>
    concept writable_channel_type
        = channel_type<T, SendType> and (T::flags & channel_flags::writable);

    template <typename T, typename SendType>
    concept bidirectional_channel_type
        = readable_channel_type<T, SendType> and writable_channel_type<T, SendType>;
    // clang-format on
}  // namespace asiochan
