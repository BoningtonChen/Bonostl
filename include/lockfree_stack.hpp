#pragma once

#include "bonostlpch.h"

namespace Bonostl
{
    inline constexpr unsigned max_hazard_pointers = 100;

    struct hazard_pointer
    {
        std::atomic<std::thread::id> id;
        std::atomic<void*> pointer;
    };

    inline std::array<hazard_pointer, max_hazard_pointers> hazard_pointers;

    class hp_owner
    {
    public:
        hp_owner(const hp_owner&) = delete;
        hp_owner& operator=(const hp_owner&) = delete;

        hp_owner()
        {
            for (auto& hp : hazard_pointers)
            {
                std::thread::id old_id;

                if (hp.id.compare_exchange_strong(
                        old_id, std::this_thread::get_id(),
                        std::memory_order_acquire, std::memory_order_relaxed))
                {
                    hp_ = &hp;
                    break;
                }
            }

            if (hp_ == nullptr)
            {
                throw std::runtime_error("No hazard pointers available!");
            }
        }

        ~hp_owner()
        {
            hp_->pointer.store(nullptr, std::memory_order_release);
            hp_->id.store(std::thread::id(), std::memory_order_release);
        }

        [[nodiscard]] std::atomic<void*>& get_pointer() const noexcept
        {
            return hp_->pointer;
        }

    private:
        hazard_pointer* hp_ = nullptr;
    };

    inline std::atomic<void*>& get_hazard_pointer_for_current_thread()
    {
        thread_local static hp_owner hazard;
        return hazard.get_pointer();
    }

    inline bool outstanding_hazard_pointers_for(void* p)
    {
        return std::ranges::any_of(hazard_pointers, [p](auto const& hp) {
            return hp.pointer.load(std::memory_order_acquire) == p;
        });
    }

    template<typename T>
    void do_delete(void* p)
    {
        delete static_cast<T*>(p);
    }

    struct data_to_reclaim
    {
        void* data;
        std::move_only_function<void(void*)> deleter;
        data_to_reclaim* next;

        template<typename T>
        explicit data_to_reclaim(T* p)
            : data(p), deleter(&do_delete<T>), next(nullptr)
        {
        }

        ~data_to_reclaim()
        {
            deleter(data);
        }
    };

    inline std::atomic<data_to_reclaim*> nodes_to_reclaim;

    inline void add_to_reclaim_list(data_to_reclaim* node)
    {
        node->next = nodes_to_reclaim.load(std::memory_order_acquire);

        while (!nodes_to_reclaim.compare_exchange_weak(
                node->next, node, std::memory_order_release, std::memory_order_relaxed))
        {
        }
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
            {
                delete current;
            }
            else
            {
                add_to_reclaim_list(current);
            }

            current = next;
        }
    }

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
