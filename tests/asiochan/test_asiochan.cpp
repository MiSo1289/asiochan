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

#endif

namespace asio = asiochan::asio;

// TODO debug this

auto main() -> int
{
    auto ioc = asio::io_context{};
    auto chan = asiochan::channel<int>{ioc.get_executor()};

    asio::co_spawn(
        ioc,
        [chan]() mutable -> asio::awaitable<void> {
            co_await chan.write(EXIT_SUCCESS);
        },
        asio::detached);

    auto reader = asio::co_spawn(
        ioc,
        [chan]() mutable -> asio::awaitable<int> {
            co_return co_await chan.read();
        },
        asio::use_future);

    ioc.run();

    return reader.get();
}
