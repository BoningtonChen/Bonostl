#pragma once

#include "bonostlpch.h"

namespace Bonostl
{
    template<typename T>
    class lockfree_queue
    {
    private:
        struct node;

        struct counted_node_ptr
        {
            int external_count;
            node* ptr;
        };

        std::atomic<counted_node_ptr> head;
        std::atomic<counted_node_ptr> tail;

        struct node_counter
        {
            unsigned internal_count : 30;
            unsigned external_counters : 2;
        };

        struct node
        {
            std::atomic<T*> data;
            std::atomic<node_counter> count;
            std::atomic<counted_node_ptr> next;

            node() noexcept
            {
                count.store(node_counter{0, 2});
                next.store(counted_node_ptr{0, nullptr});
            }

            void release_ref() noexcept
            {
                node_counter old_counter = count.load(std::memory_order_relaxed);
                node_counter new_counter;

                do
                {
                    new_counter = old_counter;
                    --new_counter.internal_count;
                } while (!count.compare_exchange_strong(
                        old_counter, new_counter,
                        std::memory_order_acquire, std::memory_order_relaxed));

                if (new_counter.internal_count == 0 && new_counter.external_counters == 0)
                {
                    delete this;
                }
            }
        };

        static void increase_external_count(
            std::atomic<counted_node_ptr>& counter, counted_node_ptr& old_counter) noexcept
        {
            counted_node_ptr new_counter;

            do
            {
                new_counter = old_counter;
                ++new_counter.external_count;
            } while (!counter.compare_exchange_strong(
                    old_counter, new_counter,
                    std::memory_order_acquire, std::memory_order_relaxed));

            old_counter.external_count = new_counter.external_count;
        }

        static void free_external_counter(counted_node_ptr& old_node_ptr) noexcept
        {
            node* const ptr = old_node_ptr.ptr;

            int const count_increase = old_node_ptr.external_count - 2;

            node_counter old_counter = ptr->count.load(std::memory_order_relaxed);
            node_counter new_counter;

            do
            {
                new_counter = old_counter;
                --new_counter.external_counters;
                new_counter.internal_count += count_increase;
            } while (!ptr->count.compare_exchange_strong(
                    old_counter, new_counter,
                    std::memory_order_acquire, std::memory_order_relaxed));

            if (new_counter.internal_count == 0 && new_counter.external_counters == 0)
            {
                delete ptr;
            }
        }

        void set_new_tail(counted_node_ptr& old_tail, counted_node_ptr const& new_tail) noexcept
        {
            node* const current_tail_ptr = old_tail.ptr;

            while (!tail.compare_exchange_weak(old_tail, new_tail)
                   && old_tail.ptr == current_tail_ptr)
            {
                if (old_tail.ptr == current_tail_ptr)
                {
                    free_external_counter(old_tail);
                }
                else
                {
                    current_tail_ptr->release_ref();
                }
            }
        }

    public:
        lockfree_queue()
            : head(counted_node_ptr{1, new node}), tail(head.load(std::memory_order_relaxed))
        {
        }

        lockfree_queue(const lockfree_queue&) = delete;
        lockfree_queue& operator=(const lockfree_queue&) = delete;

        ~lockfree_queue()
        {
            while (node* const old_head = head.load(std::memory_order_relaxed).ptr)
            {
                head.store(old_head->next.load(std::memory_order_relaxed), std::memory_order_relaxed);
                delete old_head;
            }
        }

        std::optional<T> pop()
        {
            counted_node_ptr old_head = head.load(std::memory_order_relaxed);

            for (;;)
            {
                increase_external_count(head, old_head);
                node* const ptr = old_head.ptr;

                if (ptr == tail.load(std::memory_order_acquire).ptr)
                {
                    return std::nullopt;
                }

                counted_node_ptr next = ptr->next.load(std::memory_order_acquire);

                if (head.compare_exchange_strong(
                        old_head, next, std::memory_order_acquire, std::memory_order_relaxed))
                {
                    T* const res = ptr->data.exchange(nullptr, std::memory_order_relaxed);
                    free_external_counter(old_head);

                    std::optional<T> result(std::move(*res));
                    delete res;
                    return result;
                }

                ptr->release_ref();
            }
        }

        void push(T new_value)
        {
            T* const new_data = new T(std::move(new_value));

            counted_node_ptr new_next;
            new_next.ptr = new node;
            new_next.external_count = 1;

            counted_node_ptr old_tail = tail.load(std::memory_order_relaxed);

            for (;;)
            {
                increase_external_count(tail, old_tail);
                T* old_data = nullptr;

                if (old_tail.ptr->data.compare_exchange_strong(
                        old_data, new_data,
                        std::memory_order_release, std::memory_order_relaxed))
                {
                    counted_node_ptr old_next{0, nullptr};

                    if (!old_tail.ptr->next.compare_exchange_strong(
                            old_next, new_next,
                            std::memory_order_release, std::memory_order_relaxed))
                    {
                        delete new_next.ptr;
                        new_next = old_next;
                    }

                    set_new_tail(old_tail, new_next);
                    break;
                }
                else
                {
                    counted_node_ptr old_next{0, nullptr};

                    if (old_tail.ptr->next.compare_exchange_strong(
                            old_next, new_next,
                            std::memory_order_release, std::memory_order_relaxed))
                    {
                        old_next = new_next;
                        new_next.ptr = new node;
                    }

                    set_new_tail(old_tail, old_next);
                }
            }
        }
    };
}
