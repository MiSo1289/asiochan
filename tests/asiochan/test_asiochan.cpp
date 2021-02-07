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

[[nodiscard]] auto reader(asiochan::read_channel<int> channel) -> asio::awaitable<int>
{
    co_return co_await channel.read();
}

[[nodiscard]] auto writer(asiochan::write_channel<int> channel) -> asio::awaitable<void>
{
    co_await channel.write(0);
}

auto main() -> int
{
    auto ioc = asio::io_context{};
    auto channel = asiochan::channel<int>{ioc.get_executor()};

    asio::co_spawn(ioc, writer(channel), asio::detached);
    auto result = asio::co_spawn(ioc, reader(channel), asio::use_future);

    ioc.run();
    return result.get();
}
