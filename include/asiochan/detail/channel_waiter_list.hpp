#pragma once

#include <concepts>
#include <cstddef>
#include <mutex>

#include "asiochan/async_promise.hpp"
#include "asiochan/detail/send_slot.hpp"
#include "asiochan/sendable.hpp"

namespace asiochan::detail
{
    using select_waiter_token = std::size_t;

    template <asio::execution::executor Executor>
    struct select_wait_context
    {
        async_promise<select_waiter_token, Executor> promise;
        std::mutex mutex;
        bool avail_flag = true;
    };

    template <asio::execution::executor Executor>
    auto claim(select_wait_context<Executor>& ctx) -> bool
    {
        auto const lock = std::scoped_lock{ctx.mutex};
        return std::exchange(ctx.avail_flag, false);
    }

    template <sendable T, asio::execution::executor Executor>
    struct channel_waiter_list_node
    {
        select_wait_context<Executor>* ctx = nullptr;
        send_slot<T>* slot = nullptr;
        select_waiter_token token = 0;
        channel_waiter_list_node* prev = nullptr;
        channel_waiter_list_node* next = nullptr;
    };

    template <sendable T, asio::execution::executor Executor>
    void notify_waiter(channel_waiter_list_node<T, Executor>& waiter)
    {
        waiter.ctx->promise.set_value(waiter.token);
    }

    template <sendable T, asio::execution::executor Executor>
    class channel_waiter_list
    {
      public:
        using node_type = channel_waiter_list_node<T, Executor>;

        void enqueue(node_type& node) noexcept
        {
            node.prev = last_;
            node.next = nullptr;

            if (not first_)
            {
                first_ = &node;
            }
            else
            {
                last_->next = &node;
            }

            last_ = &node;
        }

        void dequeue(node_type& node) noexcept
        {
            if (&node == first_)
            {
                first_ = node.next;
            }
            if (&node == last_)
            {
                last_ = node.prev;
            }
            if (node.prev)
            {
                node.prev->next = node.next;
                node.prev = nullptr;
            }
            if (node.next)
            {
                node.next->prev = node.prev;
                node.next = nullptr;
            }
        }

        auto dequeue_first_available(
            std::same_as<select_wait_context<Executor>> auto&... contexts) noexcept
            -> node_type*
        {
            while (first_)
            {
                auto const node = first_;

                auto const pop = [&]()
                {
                    first_ = node->next;
                    if (not first_)
                    {
                        last_ = nullptr;
                    }
                    else
                    {
                        first_->prev = nullptr;
                        node->next = nullptr;
                    }
                };

                auto const lock = std::scoped_lock{node->ctx->mutex, contexts.mutex...};
                if (node->ctx->avail_flag)
                {
                    if (not(contexts.avail_flag and ...))
                    {
                        return nullptr;
                    }

                    node->ctx->avail_flag = false;
                    ((contexts.avail_flag = false), ...);

                    pop();

                    return node;
                }

                pop();
            }

            return nullptr;
        }

      private:
        node_type* first_ = nullptr;
        node_type* last_ = nullptr;
    };
}  // namespace asiochan::detail
