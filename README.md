# Asio-chan
[![Build](https://github.com/MiSo1289/asiochan/workflows/Build/badge.svg)](https://github.com/MiSo1289/asiochan/actions?query=workflow%3ABuild)
```c++
#include <asiochan/asiochan.hpp>

using namespace asiochan;
```

This library provides golang-inspired channel types to be used with ASIO `awaitable` coroutines.
Channels allow bidirectional message passing and synchronization between coroutines.
Both standalone and boost versions of ASIO are supported.

### Selecting ASIO distribution

By default, Boost.ASIO is used. To use with standalone ASIO:
- When consuming as a Conan package - set the option `asio=standalone`
- When consuming as a CMake subproject - set the cache variable `ASIOCHAN_USE_STANDALONE_ASIO=ON`
- When consuming as headers - define the `ASIOCHAN_USE_STANDALONE_ASIO` macro

### Example

```c++
auto sum_subtask(
    read_channel<std::optional<int>> in, 
    write_channel<int> out) 
    -> asio::awaitable<void> 
{
    auto sum = 0;
    while (auto value = co_await in.read()) 
    {
        sum += *value;
    }

    co_await out.write(sum);
}

auto sum_task(std::span<int const> array, int num_tasks)
    -> asio::awaitable<int> 
{
    auto executor = co_await asio::this_coro::executor;

    // Spawn N child routines, sharing the same in/out channels
    auto in = channel<std::optional<int>>{executor};
    auto out = channel<int>{executor};
    for (auto i : std::views::iota(0, num_tasks))
    {
        asio::co_spawn(executor, sum_subtask(in, out), asio::detached);
    }

    // Send the array to the child routines
    for (auto val : array)
    {
        co_await in.write(val);
    }

    for (auto i : std::views::iota(0, num_tasks - 1))
    {
        // Join a task
        co_await in.write(std::nullopt);
        // Retrieve its result
        auto subresult = co_await out.read();
        // Send it to another task
        co_await in.write(subresult);
    }
    
    // Join the last task
    co_await in.write(std::nullopt);
    // Retrieve the complete result
    co_return co_await out.read();
}

auto main() -> int
{
    auto tp = asio::thread_pool{};
    
    auto numbers = std::vector<int>(100);
    std::iota(numbers.begin(), numbers.end(), 1);
    
    auto task = asio::co_spawn(tp, sum_task(numbers, 10), asio::use_future);
    std::cout << "The result is " << task.get();
    
    return 0;
}
```

### Features

- Thread safety - all channel types are thread-safe, relying on ASIO strands.
- Value semantics - channels are intended to be passed by value. Internally, a channel holds a `shared_ptr` to a shared state type, similar to `future` and `promise`. 
- Bidirectional - channels are bidirectional by default, but can be restricted to write or read only (similar to channels in golang). Write, read, and bidirectional channels are convertible between each other, as the shared state type does not change.
- Synchronization - by default, a writer will wait until someone reads the value. Readers and writers are queued in FIFO order. Similar to golang channels, it is possible to specify a buffer size; writing is wait-free as long as there is space in the buffer. A dynamically sized buffer that is always wait-free for the writer is also available.
- Channels of `void` - channels that do not send any values and are used only for synchronization are also possible. When buffered, the buffer is implemented as a simple counter (and does not allocate even when dynamically sized).

### Interface

#### Sendable
```c++
#include <asiochan/sendable.hpp>

template <typename T>
concept sendable;
```

The `sendable` concept defines the requirements for types that can be sent via channels. It is satisfied by all nothrow-movable types, and `void`.

#### Basic channel
```c++
#include <asiochan/channel.hpp>

template <sendable T, channel_buff_size buff_size, asio::execution::executor Executor>
class basic_channel;
 
template <sendable T, channel_buff_size buff_size, asio::execution::executor Executor>
class basic_read_channel;

template <sendable T, channel_buff_size buff_size, asio::execution::executor Executor>
class basic_write_channel;
```

Bidirectional, read and write channel types are interconvertible as long as the value type, buffer size, and executor match. `buff_size` (`size_t`) specifies the size of the internal buffer. When 0, the writer will always wait for a read. A special value `unbounded_channel_buff` can be used, in which case the buffer is dynamic and writers never wait.

#### Convenience typedefs
```c++
template <sendable T, channel_buff_size buff_size = 0>
using channel = basic_channel<T, buff_size, asio::any_io_executor>;

template <sendable T, channel_buff_size buff_size = 0>
using read_channel = basic_read_channel<T, buff_size, asio::any_io_executor>;

template <sendable T, channel_buff_size buff_size = 0>
using write_channel = basic_write_channel<T, buff_size, asio::any_io_executor>;

template <sendable T>
using unbounded_channel = channel<T, unbounded_channel_buff>;

template <sendable T>
using unbounded_read_channel = read_channel<T, unbounded_channel_buff>;

template <sendable T>
using unbounded_write_channel = write_channel<T, unbounded_channel_buff>;
```

#### Constructor
```c++
auto ioc = asio::io_context{};
auto chan1 = channel<void>{ioc};  // Execution context constructor
auto chan2 = channel<void>{ioc.get_executor()};  // Executor constructor
auto chan3 = chan1;  // Copy constructor - now shares state with chan1
auto chan4 = std::move(chan2);  // Move constructor - chan2 is now invalid.
```

#### Read
```c++
auto chan = channel<int>{ioc};
auto maybe_result = std::optional<int>{co_await chan.try_read()};
auto result = int{co_await chan.read()};

auto chan_void = channel<void>{ioc};
auto success = bool{co_await chan_void.try_read()};
co_await chan_void.read();
```

The `try_read` functions do not perform any waiting (`co_await` must still be used, as a `strand` is used for synchronization). If no value is available, `nullopt` (or `false` for `channel<void>`) is returned.

The `read` functions will wait until a value is ready.

#### Write
```c++
auto success = bool{co_await chan.try_write(1)};
co_await chan.write(1);

auto success = bool{co_await chan_void.try_write()};
co_await chan_void.write();
```

The `try_write` functions do not perform any waiting. If no waiter was ready and the internal buffer was full, `false` is returned.

The `write` function will wait until a reader is ready.

### Limitations
This is a 'minimal useful version', with some limitations: 
- No cancel / close support.
- No support for concurrent waiting on multiple channels.

This can usually be worked around by sending sum types, like `variant` and `optional`.
