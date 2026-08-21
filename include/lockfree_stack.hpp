#pragma once

#include "bonostlpch.h"
#include "hazard_ptr.hpp"

namespace Bonostl
{
    template<typename T>
    class lockfree_stack
    {
    private:
        struct node
        {
            std::optional<T> data;
            node* next;

            explicit node(T value)
                : data(std::move(value)), next(nullptr)
            {
            }
        };

    public:
        lockfree_stack() = default;

        lockfree_stack(const lockfree_stack&) = delete;
        lockfree_stack& operator=(const lockfree_stack&) = delete;

        ~lockfree_stack()
        {
            while (pop())
            {
            }
        }

        void push(T value)
        {
            node* const new_node = new node(std::move(value));
            new_node->next = head.load(std::memory_order_relaxed);

            while (!head.compare_exchange_weak(
                    new_node->next, new_node,
                    std::memory_order_release, std::memory_order_relaxed))
            {
            }
        }

        std::optional<T> pop()
        {
            std::atomic<void*>& hp = get_hazard_pointer_for_current_thread();
            node* old_head = head.load(std::memory_order_relaxed);

            do
            {
                node* temp;

                do
                {
                    temp = old_head;
                    hp.store(old_head, std::memory_order_release);
                    old_head = head.load(std::memory_order_relaxed);
                } while (old_head != temp);
            } while (old_head != nullptr
                     && !head.compare_exchange_strong(
                            old_head, old_head->next,
                            std::memory_order_acquire, std::memory_order_relaxed));

            hp.store(nullptr, std::memory_order_release);

            if (old_head == nullptr)
            {
                return std::nullopt;
            }

            std::optional<T> result = std::move(old_head->data);

            if (outstanding_hazard_pointers_for(old_head))
            {
                reclaim_later(old_head);
            }
            else
            {
                delete old_head;
            }

            return result;
        }

    private:
        std::atomic<node*> head{nullptr};
    };
}
