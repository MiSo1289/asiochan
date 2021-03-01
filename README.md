# Asio-chan
[![Build](https://github.com/MiSo1289/asiochan/workflows/Build/badge.svg)](https://github.com/MiSo1289/asiochan/actions?query=workflow%3ABuild)
```c++
#include <asiochan/asiochan.hpp>

using namespace asiochan;
```

This library provides golang-inspired channel types to be used with ASIO `awaitable` coroutines.
Channels allow bidirectional message passing and synchronization between coroutines.
Both standalone and boost versions of ASIO are supported.
See the [installing](#installing) section on how to install and select the ASIO distribution used.

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

- Thread safety - all channel types are thread-safe.
- Value semantics - channels are intended to be passed by value. Internally, a channel holds a `shared_ptr` to a shared state type, similar to `future` and `promise`. 
- Bidirectional - channels are bidirectional by default, but can be restricted to write or read only (similar to channels in golang).
- Synchronization - by default, a writer will wait until someone reads the value. Readers and writers are queued in FIFO order. Similar to golang channels, it is possible to specify a buffer size; writing is wait-free as long as there is space in the buffer. A dynamically sized buffer that is always wait-free for the writer is also available.
- Channels of `void` - channels that do not send any values and are used only for synchronization are also possible. When buffered, the buffer is implemented as a simple counter (and does not allocate even when dynamically sized).
- It is possible to simultaneously await multiple alternative read / write channel operations, similar to go's `select` statement, see [select](#select). This allows e.g. for easy implementation of cancellation / timeouts.

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

Bidirectional channels can be converted to matching read and write channel types as long as the value type, buffer size, and executor match. Read and write channels are not interconvertible, to preserve type-safety. `buff_size` (`size_t`) specifies the size of the internal buffer. When 0, the writer will always wait for a read. A special value `unbounded_channel_buff` can be used, in which case the buffer is dynamic and writers never wait.

#### Convenience typedefs
```c++
template <sendable T, channel_buff_size buff_size_ = 0>
using channel = basic_channel<T, buff_size_, asio::any_io_executor>;

template <sendable T, channel_buff_size buff_size_ = 0>
using read_channel = basic_read_channel<T, buff_size_, asio::any_io_executor>;

template <sendable T, channel_buff_size buff_size_ = 0>
using write_channel = basic_write_channel<T, buff_size_, asio::any_io_executor>;

template <sendable T>
using unbounded_channel = channel<T, unbounded_channel_buff>;

template <sendable T>
using unbounded_read_channel = read_channel<T, unbounded_channel_buff>;

template <sendable T>
using unbounded_write_channel = write_channel<T, unbounded_channel_buff>;
```

#### Constructor
```c++
asio::io_context ioc{};
channel<void> chan1{ioc};  // Execution context constructor
channel<void> chan2{ioc.get_executor()};  // Executor constructor
auto chan3 = chan1;  // Copy constructor - now shares state with chan1
auto chan4 = std::move(chan2);  // Move constructor - chan2 is now invalid.
```

#### Read
```c++
channel<int> chan{ioc};
std::optional<int> maybe_result = chan.try_read();
int result = co_await chan.read();

channel<void> chan_void{ioc};
bool success = chan_void.try_read();
co_await chan_void.read();
```

The `try_read` method does not perform any waiting. If no value is available, `nullopt` (or `false` for `channel<void>`) is returned.

The `read` method will wait until a value is ready.

#### Write
```c++
bool success = chan.try_write(1);
co_await chan.write(1);

bool success = chan_void.try_write();
co_await chan_void.write();
```

The `try_write` method do not perform any waiting. If no waiter was ready and the internal buffer was full, `false` is returned.

The `write` method will wait until a reader is ready.

Note that for unbounded buffered channels, writing always succeeds and is without wait. To reflect this fact, the `try_write` method is not available, and `write` can be called without `co_await`.

#### Select
```c++
#include <asiochan/select.hpp>
```

The `select` function allows awaiting on multiple alternative channel operations. The first ready operation will cancel all others. Cancellation is fully deterministic. For example, when you await reads on two different channels, only one of these will have a value consumed.

```c++
channel<void> chan_void_1{ioc};
channel<void> chan_void_2{ioc};
channel<int> chan_int_1{ioc};
channel<int> chan_int_2{ioc};

auto result = co_await select(
    ops::read(chan_void_1),
    ops::write(chan_void_2),
    ops::read(chan_int_1),
    ops::write(std::rand(), chan_int_2));

bool received_void = result.received<void>();
bool sent_void = result.sent<void>();
// Non-owning pointer inside the result object if int was received, nullptr otherwise.
int* maybe_received_int = result.get_if_received<int>();
bool sent_int = result.sent<int>();

if (result.received<int>())
{
    // The get_received<T> method will throw bad_select_result_access if you get the type wrong.
    int received_int = result.get_received<int>();
}
```

If you don't want to wait until some operation becomes ready, you can use the wait-free function `select_ready`. It must be passed some default wait-free operation as the last argument. An example of a wait-free operation is `nothing`:

```c++
auto result = select_ready(
    ops::read(chan_int_1),
    ops::write(std::rand(), chan_int_2),
    ops::nothing);

// If nothing is an alternative, has_value() method is available...
bool any_succeeded = result.has_value();
// .. and the result is contextually convertible to bool.
if (result)
{
    // ...
}
```

Writing to an unbounded channel is also a wait-free operation, and can be thus be used as the default operation for `select_ready`.

The `read` and `write` operations can accept multiple channels.
This allows you to `select` between multiple write channels with the same `send_type` without copying the sent value:
```c++
channel<std::string> chan_1{ioc};
channel<std::string> chan_2{ioc};
std::string long_string = "...";

auto string_send_result = co_await select(
    ops::write(std::move(long_string), chan_1, chan_2));
```

The `select_result` type remembers the shared state of the channel for which an operation succeeded. This allows disambiguation between channels of the same `send_type`:

```c++
bool sent_to_chan_1 = string_send_result.sent_to(chan_1);
bool sent_to_chan_2 = string_send_result.sent_to(chan_2);

auto string_recv_result = co_await select(
    ops::read(chan_1, chan_2));

bool recv_from_chan_1 = string_recv_result.received_from(chan_1);
bool recv_from_chan_2 = string_recv_result.received_from(chan_2);
// Similar to get_if<T>()
std::string* result = string_recv_result.get_if_received_from(chan_1);
```

##### Example: timeouts

The select feature can be useful for implementing timeouts on channel operations.

```c++
using std::chrono::steady_clock;
using duration = steady_clock::duration;
using namespace std::literals;

auto set_timeout(
    asio::execution::executor auto executor,
    duration dur)
    -> read_channel<void>
{
    auto timer = asio::steady_timer{executor};
    timer.expires_after(dur);
    
    auto timeout = channel<void>{executor};

    asio::co_spawn(
        executor,
        [=]() -> asio::awaitable<void> {
            co_await timer.async_wait(asio::use_awaitable);
            co_await timeout.write();
        },
        asio::detached);

    return timeout;
}

auto accept_client_requests(
    write_channel<std::string> requests_channel)
    -> asio::awaitable<void>
{
    while (true)
    {
        auto request_from_client = co_await /* ... */;
        co_await requests_channel.write(std::move(request_from_client));
    }
}

auto timeout_example()
    -> asio::awaitable<void>
{
    auto executor = co_await asio::this_coro::executor;
    auto requests = channel<std::string>{executor};

    asio::co_spawn(
        executor, 
        accept_client_requests(requests_channel), 
        asio::detached);

    auto timeout = set_timeout(executor, 10s);
    auto result = co_await select(
        ops::read(requests),
        ops::read(timeout));
    
    if (auto* request = result.get_if_received<std::string>())
    {
        // Handle request...
    }
    else
    {
        // Handle timeout...
    }
}
```

### Installing

#### Selecting ASIO distribution

By default, Boost.ASIO is used. To use with standalone ASIO:
- When consuming as a Conan package - set the option `asio=standalone`
- When consuming as a CMake subproject - set the cache variable `ASIOCHAN_USE_STANDALONE_ASIO=ON`
- When consuming as headers - define the `ASIOCHAN_USE_STANDALONE_ASIO` macro

#### Conan package

If you use Conan to manage dependencies, you can get this library from [my artifactory](https://miso1289.jfrog.io/ui/packages/conan:%2F%2Fasiochan?name=asiochan&type=packages):
```console
$ conan remote add miso1289 https://miso1289.jfrog.io/artifactory/api/conan/miso1289
$ conan install asiochan/0.3.0@miso1289/stable
```
