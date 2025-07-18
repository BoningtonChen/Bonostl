//
// Created by 陈奕锟 on 2022/12/3.
//

#ifndef BONOSTL_LOCKFREE_STACK_HPP
#define BONOSTL_LOCKFREE_STACK_HPP
#endif //BONOSTL_LOCKFREE_STACK_HPP
#pragma once

#include "bonostlpch.h"

namespace Bonostl
{
    constexpr unsigned max_hazard_pointers = 100;

    struct hazard_pointer
    {
        std::atomic<std::thread::id> id;
        std::atomic<void*> pointer;
    };

    inline std::array<hazard_pointer, max_hazard_pointers> hazard_pointers;

    class hp_owner
    {
    private:
        hazard_pointer* hp;

    public:
        hp_owner(hp_owner const&) = delete;

        hp_owner operator=(hp_owner const&) = delete;

        hp_owner()
                : hp(nullptr)
        {
            for (auto& hazard_pointer: hazard_pointers)
            {
                std::thread::id old_id;

                if (hazard_pointer.id.compare_exchange_strong(
                        old_id, std::this_thread::get_id(), std::memory_order_acquire, std::memory_order_relaxed
                ))
                {
                    hp = &hazard_pointer;
                    break;
                }
            }

            if (!hp)
            {
                throw std::runtime_error("No hazard pointers available!");
            }
        }

        ~hp_owner()
        {
            hp->pointer.store(nullptr, std::memory_order_release);
            hp->id.store(std::thread::id(), std::memory_order_release);
        }

        [[nodiscard]] std::atomic<void*>& get_pointer() const {
            return hp->pointer;
        }
    };

    inline std::atomic<void*>& get_hazard_pointer_for_current_thread()
    {
        thread_local static hp_owner hazard;

        return hazard.get_pointer();
    }

    inline bool outstanding_hazard_pointers_for(void* p)
    {
        return std::ranges::any_of(hazard_pointers, [&](auto& hp) { return hp.pointer.load(std::memory_order_acquire) == p; });
    }

    template<typename T>
    void do_delete(void* p)
    {
        delete static_cast<T*>(p);
    }

    struct data_to_reclaim
    {
        void* data;
        std::function<void(void*)> deleter;
        data_to_reclaim* next;

        template<typename T>
        explicit data_to_reclaim(T* p)
                : data(p), deleter(&do_delete<T>), next(nullptr)
        {}

        ~data_to_reclaim()
        {
            deleter(data);
        }
    };

    inline std::atomic<data_to_reclaim*> nodes_to_reclaim;

    inline void add_to_reclaim_list(data_to_reclaim* node)
    {
        node->next = nodes_to_reclaim.load(std::memory_order_acquire);

        while (!nodes_to_reclaim.compare_exchange_weak(node->next, node, std::memory_order_release, std::memory_order_relaxed));
    }

    template<typename T>
    void reclaim_later(T* data)
    {
        add_to_reclaim_list(new data_to_reclaim(data));
    }

    inline void delete_nodes_with_no_hazards()
    {
        data_to_reclaim* current = nodes_to_reclaim.exchange(nullptr, std::memory_order_acquire);

        while (current)
        {
            data_to_reclaim* const next = current->next;

            if (!outstanding_hazard_pointers_for(current->data))
                delete current;
            else
                add_to_reclaim_list(current);

            current = next;
        }
    }

    template<typename T>
    class lockfree_stack
    {
    private:
        struct node;

        struct counted_node_ptr
        {
            int external_count;
            node* ptr;
        };

        struct node
        {
            std::shared_ptr<T> data;
            std::atomic<int> internal_count;
            counted_node_ptr next;

            explicit node(T const& i_data)
                    : data(std::make_shared<T>(i_data)), internal_count(0)
            {}
        };

        std::atomic<counted_node_ptr> head;
        std::atomic<unsigned> threads_in_pop;
        std::atomic<node*> to_be_deleted;

        void increase_head_count(counted_node_ptr& old_counter)
        {
            counted_node_ptr new_counter;
            do
            {
                new_counter = old_counter;
                ++new_counter.external_count;
            }
            while (!head.compare_exchange_strong(
                    old_counter, new_counter, std::memory_order_acquire, std::memory_order_relaxed
            ));

            old_counter.external_count = new_counter.external_count;
        }

        void try_reclaim(node* old_head)
        {
            --threads_in_pop;
            if (threads_in_pop == 0)
            {
                node* nodes_to_delete = to_be_deleted.exchange(nullptr, std::memory_order_acquire);

                delete_nodes(nodes_to_delete);
                delete old_head;
            }
            else
            {
                chain_pending_node(old_head);
            }
        }

        void chain_pending_nodes(node* nodes)
        {
            node* last = nodes;

            while (node* const next = last->next.ptr)
                last = next;

            chain_pending_nodes(nodes, last);
        }

        void chain_pending_nodes(node* first, node* last)
        {
            last->next.ptr = to_be_deleted.load(std::memory_order_acquire);

            while (!to_be_deleted.compare_exchange_weak(
                    last->next.ptr, first, std::memory_order_release, std::memory_order_relaxed
            ));
        }

        void chain_pending_node(node* n)
        {
            chain_pending_nodes(n, n);
        }

    public:
        lockfree_stack()
                : threads_in_pop(0), to_be_deleted(nullptr)
        {}

        ~lockfree_stack()
        {
            while (pop());
        }

        void push(T const& data)
        {
            counted_node_ptr new_node;
            new_node.ptr = new node(data);
            new_node.external_count = 1;
            new_node.ptr->next = head.load(std::memory_order_relaxed);

            while (!head.compare_exchange_weak(
                    new_node.ptr->next, new_node, std::memory_order_release, std::memory_order_relaxed
            ));
        }

        std::shared_ptr<T> pop()
        {
            counted_node_ptr old_head = head.load(std::memory_order_relaxed);

            for (;;)
            {
                increase_head_count(old_head);
                node* const ptr = old_head.ptr;

                if (!ptr)
                {
                    return std::shared_ptr<T>();
                }
                if (head.compare_exchange_strong(
                        old_head, ptr->next, std::memory_order_relaxed
                ))
                {
                    std::shared_ptr<T> res;
                    res.swap(ptr->data);

                    int const count_increase = old_head.external_count - 2;

                    if (ptr->internal_count.fetch_add(
                            count_increase, std::memory_order_release
                    ) == -count_increase)
                    {
                        delete ptr;
                    }
                    return res;
                }
                else if (ptr->internal_count.fetch_add(-1, std::memory_order_relaxed) == 1)
                {
                    ptr->internal_count.load(std::memory_order_acquire);
                    delete ptr;
                }
            }
        }
    };
}