#pragma once

#include "bonostlpch.h"

namespace Bonostl
{
    template<typename T>
    class threadsafe_list
    {
    private:
        struct node
        {
            std::mutex m;
            std::shared_ptr<T> data;
            std::unique_ptr<node> next;

            node() = default;

            explicit node(T value)
                : data(std::make_shared<T>(std::move(value)))
            {
            }
        };

        node head;

    public:
        threadsafe_list() = default;

        ~threadsafe_list()
        {
            remove_if([](T const&) { return true; });
        }

        threadsafe_list(const threadsafe_list&) = delete;
        threadsafe_list& operator=(const threadsafe_list&) = delete;

        void push_front(T value)
        {
            std::unique_ptr<node> new_node = std::make_unique<node>(std::move(value));
            std::lock_guard lock(head.m);

            new_node->next = std::move(head.next);
            head.next = std::move(new_node);
        }

        template<typename Function>
        void for_each(Function func)
        {
            node* current = &head;
            std::unique_lock<std::mutex> lk(head.m);

            while (node* const next = current->next.get())
            {
                std::unique_lock<std::mutex> next_lk(next->m);
                lk.unlock();

                func(*next->data);
                current = next;
                lk = std::move(next_lk);
            }
        }

        template<typename Predicate>
        std::shared_ptr<T> find_first_if(Predicate p)
        {
            node* current = &head;
            std::unique_lock<std::mutex> lk(head.m);

            while (node* const next = current->next.get())
            {
                std::unique_lock<std::mutex> next_lk(next->m);
                lk.unlock();

                if (p(*next->data))
                {
                    return next->data;
                }

                current = next;
                lk = std::move(next_lk);
            }

            return {};
        }

        template<typename Predicate>
        void remove_if(Predicate p)
        {
            node* current = &head;
            std::unique_lock<std::mutex> lk(head.m);

            while (node* const next = current->next.get())
            {
                std::unique_lock<std::mutex> next_lk(next->m);

                if (p(*next->data))
                {
                    std::unique_ptr<node> old_next = std::move(current->next);
                    current->next = std::move(next->next);
                    next_lk.unlock();
                }
                else
                {
                    lk.unlock();
                    current = next;
                    lk = std::move(next_lk);
                }
            }
        }
    };
}
