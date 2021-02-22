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
        typename T::send_type;

        requires detail::channel_shared_state_type<typename T::shared_state_type, typename T::send_type>;

        { channel.shared_state() } noexcept -> std::same_as<typename T::shared_state_type&>;
    };

    template <typename T>
    concept any_readable_channel_type
        = any_channel_type<T> and (T::flags & channel_flags::readable);

    template <typename T>
    concept any_writable_channel_type
        = any_channel_type<T> and (T::flags & channel_flags::writable);

    template <typename T>
    concept any_bidirectional_channel_type
        = any_readable_channel_type<T> and any_writable_channel_type<T>;

    template <typename T, typename SendType>
    concept channel_type
        = any_channel_type<T> and std::same_as<SendType, typename T::send_type>;

    template <typename T, typename SendType>
    concept readable_channel_type
        = channel_type<T, SendType> and any_readable_channel_type<T>;

    template <typename T, typename SendType>
    concept writable_channel_type
        = channel_type<T, SendType> and any_readable_channel_type<T>;

    template <typename T, typename SendType>
    concept bidirectional_channel_type
        = channel_type<T, SendType> and any_bidirectional_channel_type<T>;
    // clang-format on
}  // namespace asiochan
