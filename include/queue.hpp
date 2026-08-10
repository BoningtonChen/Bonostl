#pragma once

#include "bonostlpch.h"

namespace Bonostl
{
    template<typename T>
    class queue
    {
    public:
        queue()
            : head(std::make_unique<node>()), tail(head.get())
        {
        }

        queue(const queue&) = delete;
        queue& operator=(const queue&) = delete;

        std::optional<T> try_pop()
        {
            if (head.get() == tail)
            {
                return std::nullopt;
            }

            std::optional<T> result = std::move(head->data);
            head = std::move(head->next);
            return result;
        }

        void push(T new_value)
        {
            auto new_tail = std::make_unique<node>();

            tail->data = std::move(new_value);
            node* const next_tail = new_tail.get();
            tail->next = std::move(new_tail);
            tail = next_tail;
        }

    private:
        struct node
        {
            std::optional<T> data;
            std::unique_ptr<node> next;
        };

        std::unique_ptr<node> head;
        node* tail;
    };
}
