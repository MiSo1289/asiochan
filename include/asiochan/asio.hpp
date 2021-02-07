#pragma once

#ifdef ASIOCHAN_USE_STANDALONE_ASIO

#include <system_error>

#include <asio/any_io_executor.hpp>
#include <asio/async_result.hpp>
#include <asio/awaitable.hpp>
#include <asio/execution/executor.hpp>
#include <asio/execution_context.hpp>
#include <asio/dispatch.hpp>
#include <asio/post.hpp>
#include <asio/strand.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_awaitable.hpp>

#else

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/async_result.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/execution/executor.hpp>
#include <boost/asio/execution_context.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/system/error_code.hpp>

#endif

namespace asiochan
{
#ifdef ASIOCHAN_USE_STANDALONE_ASIO
    namespace asio = ::asio;
    namespace system = ::std;
#else
    namespace asio = ::boost::asio;
    namespace system = ::boost::system;
#endif
}  // namespace asiochan
