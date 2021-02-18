#pragma once

#include <atomic>
#include <cstddef>

#include "asiochan/awaitable_promise.hpp"
#include "asiochan/detail/send_slot.hpp"

namespace asiochan::detail
{
    using select_waiter_token = std::size_t;

    struct select_wait_context
    {
        awaitable_promise<select_waiter_token> promise;
        std::atomic_bool avail_flag = true;
    };

    template <sendable T>
    struct channel_waiter_list_node
    {
        select_wait_context* select_wait_context = nullptr;
        send_slot<T>* slot = nullptr;
        select_waiter_token token = 0;
        channel_waiter_list_node* next = nullptr;
    };

    template <sendable T>
    void notify_waiter(channel_waiter_list_node<T>& waiter)
    {
        waiter.select_wait_context->promise.set_value(waiter.token);
    }

    template <sendable T>
    class channel_waiter_list
    {
      public:
        using node_type = channel_waiter_list_node<T>;

        void enqueue_waiter(node_type& node) noexcept
        {
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

        auto dequeue_waiter() noexcept -> node_type*
        {
            while (first_)
            {
                auto const node = first_;
                first_ = node->next;
                if (not node)
                {
                    last_ = nullptr;
                }

                if (node->select_wait_context->avail_flag.exchange(false))
                {
                    return node;
                }
            }

            return nullptr;
        }

      private:
        node_type* first_ = nullptr;
        node_type* last_ = nullptr;
    };

}  // namespace asiochan::detail
