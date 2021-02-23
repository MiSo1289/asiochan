#pragma once

namespace asiochan::detail
{
    template <typename... Ts>
    struct overloaded : Ts...
    {
        using Ts::operator()...;
    };

    template <typename... Ts>
    overloaded(Ts...) -> overloaded<Ts...>;
}  // namespace asiochan::detail
