#include <cstdlib>
#include <iostream>
#include <numeric>
#include <optional>
#include <ranges>
#include <span>
#include <vector>

#include <asiochan/asiochan.hpp>

#ifdef ASIOCHAN_USE_STANDALONE_ASIO

#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>

#else

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/use_future.hpp>

namespace asio = boost::asio;

#endif


auto main() -> int
{
    auto ioc = asio::io_context{};
    auto s = asio::make_strand(ioc.get_executor());
    auto task = asio::co_spawn(
        ioc,
        [&]() -> asio::awaitable<void> {
            std::cout << "wuh" << std::endl;
            co_await asio::dispatch(s, asio::use_awaitable);
            co_await asio::post(s, asio::use_awaitable);
            co_await asio::dispatch(s, asio::use_awaitable);
            std::cout << "huh" << std::endl;
        },
        asio::use_future);
    ioc.run_one();
    ioc.run_one();
    task.get();
}
