#pragma once

#include <cstddef>
#include <limits>

namespace asiochan
{
    using channel_buff_size = std::size_t;

    inline constexpr auto unbounded_channel_buff = std::numeric_limits<channel_buff_size>::max();
}  // namespace asiochan
