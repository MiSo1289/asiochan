#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>

#include "asiochan/asio.hpp"
#include "asiochan/async_promise.hpp"
#include "asiochan/detail/channel_waiter_list.hpp"
#include "asiochan/select_concepts.hpp"
#include "asiochan/select_result.hpp"

namespace asiochan::detail
{
    template <select_op... Ops>
    inline constexpr auto select_ops_base_tokens = []()
    {
        auto token_base = std::size_t{0};
        return std::array{
            std::exchange(token_base, token_base + Ops::num_alternatives)...,
        };
    }();
}  // namespace asiochan::detail
