#pragma once

#include "bonostlpch.h"

namespace Bonostl
{
    template<typename T>
    class threadsafe_queue
    {
    public:
        threadsafe_queue()
            : head(std::make_unique<node>()), tail(head.get())
        {
        }

        threadsafe_queue(const threadsafe_queue&) = delete;
        threadsafe_queue& operator=(const threadsafe_queue&) = delete;

        void push(T new_value)
        {
            auto new_tail = std::make_unique<node>();
            {
                std::lock_guard tail_lock(tail_mutex_);

                tail->data = std::move(new_value);
                node* const next_tail = new_tail.get();
                tail->next = std::move(new_tail);
                tail = next_tail;
            }
            data_cond_.notify_one();
        }

        T wait_and_pop()
        {
            std::unique_ptr<node> const old_head = wait_pop_head();
            return std::move(*old_head->data);
        }

        void wait_and_pop(T& value)
        {
            std::unique_ptr<node> const old_head = wait_pop_head(value);
        }

        std::optional<T> try_pop()
        {
            std::unique_ptr<node> old_head = try_pop_head();

            return old_head ? std::optional<T>(std::move(*old_head->data)) : std::nullopt;
        }

        bool try_pop(T& value)
        {
            std::unique_ptr<node> const old_head = try_pop_head(value);
            return old_head != nullptr;
        }

        bool empty() const
        {
            std::lock_guard head_lock(head_mutex_);
            return head.get() == get_tail();
        }

    private:
        struct node
        {
            std::optional<T> data;
            std::unique_ptr<node> next;
        };

        mutable std::mutex head_mutex_;
        std::unique_ptr<node> head;
        mutable std::mutex tail_mutex_;
        node* tail;
        std::condition_variable data_cond_;

        node* get_tail() const
        {
            std::lock_guard tail_lock(tail_mutex_);
            return tail;
        }

        std::unique_ptr<node> pop_head()
        {
            std::unique_ptr<node> old_head = std::move(head);
            head = std::move(old_head->next);

            return old_head;
        }

        std::unique_lock<std::mutex> wait_for_data()
        {
            std::unique_lock<std::mutex> head_lock(head_mutex_);
            data_cond_.wait(head_lock, [&] { return head.get() != get_tail(); });

            return head_lock;
        }

        std::unique_ptr<node> wait_pop_head()
        {
            std::unique_lock<std::mutex> head_lock(wait_for_data());
            return pop_head();
        }

        std::unique_ptr<node> wait_pop_head(T& value)
        {
            std::unique_lock<std::mutex> head_lock(wait_for_data());
            value = std::move(*head->data);

            return pop_head();
        }

        std::unique_ptr<node> try_pop_head()
        {
            std::lock_guard head_lock(head_mutex_);

            if (head.get() == get_tail())
            {
                return nullptr;
            }

            return pop_head();
        }

        std::unique_ptr<node> try_pop_head(T& value)
        {
            std::lock_guard head_lock(head_mutex_);

            if (head.get() == get_tail())
            {
                return nullptr;
            }

            value = std::move(*head->data);
            return pop_head();
        }
    };
}
