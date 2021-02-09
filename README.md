# Asio-chan
[![Build](https://github.com/MiSo1289/asiochan/workflows/Build/badge.svg)](https://github.com/MiSo1289/asiochan/actions?query=workflow%3ABuild)

## Channels
```c++
#include <asiochan/channel.hpp>
```

### Example

```c++
auto sum_task(
    asiochan::read_channel<std::optional<int>> in, 
    asiochan::write_channel<int> out) 
    -> asio::awaitable<void> 
{
    auto sum = 0;
    while (auto value = co_await in.read()) 
    {
        sum += *value;
    }

    co_await out.write(sum);
}

auto sum_array(std::span<int const> array, int num_tasks)
    -> asio::awaitable<int> 
{
    auto executor = co_await asio::this_coro::executor;

    // Spawn N subtasks
    auto in = asiochan::channel<std::optional<int>>{};
    auto out = asiochan::channel<int>{};
    for (auto i : std::views::iota(0, num_tasks))
    {
        co_spawn(executor, sum_task(in, out), asio::detached);
    }

    // Send the array to the sum tasks
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
```
