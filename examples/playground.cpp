#include <cstdlib>
#include <iostream>
#include <numeric>
#include <optional>
#include <ranges>
#include <span>
#include <vector>

//#include <asiochan/asiochan.hpp>
#include <asiochan/asio.hpp>

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
    using namespace std::literals;

    auto ioc = asio::io_context{};
    auto s = asio::make_strand(ioc.get_executor());
    auto task = asio::co_spawn(
        ioc,
        [&]() -> asio::awaitable<void> {
            try
            {
                co_await asio::dispatch(s, asio::use_awaitable);
            }
            catch(...)
            {
            }
            std::cout << "1" << std::endl;
            asio::post(s, []() {
                std::cout << "2" << std::endl;
            });
            std::this_thread::sleep_for(5s);
            std::cout << "3" << std::endl;
        },
        asio::use_future);
    ioc.run();
    task.get();
}
