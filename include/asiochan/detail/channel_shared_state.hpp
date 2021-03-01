#pragma once

#include <type_traits>

#include "asiochan/asio.hpp"
#include "asiochan/channel_buff_size.hpp"
#include "asiochan/detail/channel_buffer.hpp"
#include "asiochan/detail/channel_waiter_list.hpp"
#include "asiochan/detail/send_slot.hpp"
#include "asiochan/sendable.hpp"

namespace asiochan::detail
{
    template <sendable T, bool enabled>
    class channel_shared_state_writer_list_base
    {
      public:
        using writer_list_type = channel_waiter_list<T>;

        static constexpr bool write_never_waits = false;

        [[nodiscard]] auto writer_list() noexcept -> writer_list_type&
        {
            return writer_list_;
        }

      private:
        writer_list_type writer_list_;
    };

    template <sendable T>
    class channel_shared_state_writer_list_base<T, false>
    {
      public:
        using writer_list_type = void;

        static constexpr bool write_never_waits = true;
    };

    template <sendable T, channel_buff_size buff_size_>
    class channel_shared_state
      : public channel_shared_state_writer_list_base<T, buff_size_ != unbounded_channel_buff>
    {
      public:
        using mutex_type = std::mutex;
        using buffer_type = channel_buffer<T, buff_size_>;
        using reader_list_type = channel_waiter_list<T>;

        static constexpr auto buff_size = buff_size_;

        [[nodiscard]] auto reader_list() noexcept -> reader_list_type&
        {
            return reader_list_;
        }

        [[nodiscard]] auto buffer() noexcept -> buffer_type&
        {
            return buffer_;
        }

        [[nodiscard]] auto mutex() noexcept -> mutex_type&
        {
            return mutex_;
        }

      private:
        mutex_type mutex_;
        reader_list_type reader_list_;
        [[no_unique_address]] buffer_type buffer_;
    };

    template <typename T, typename SendType>
    struct is_channel_shared_state
      : std::false_type
    {
    };

    template <sendable SendType, channel_buff_size buff_size>
    struct is_channel_shared_state<
        channel_shared_state<SendType, buff_size>,
        SendType>
      : std::true_type
    {
    };

    template <typename T, sendable SendType>
    inline constexpr auto is_channel_shared_state_type_v
        = is_channel_shared_state<T, SendType>::value;

    template <typename T, typename SendType>
    concept channel_shared_state_type
        = is_channel_shared_state_type_v<T, SendType>;
}  // namespace asiochan::detail
