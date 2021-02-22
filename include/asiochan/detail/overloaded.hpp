#pragma once

namespace asiochan::detail
{
    template <class... Ts>
    struct overloaded : Ts...
    {
        using Ts::operator()...;
    };

    template <class... Ts>
    overloaded(Ts...) -> overloaded<Ts...>;
}  // namespace asiochan::detail
