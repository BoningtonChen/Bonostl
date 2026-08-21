#pragma once

#include <cstdint>

#include "bonostlpch.h"
#include "hazard_ptr.hpp"

namespace Bonostl
{
    /// Lock-free ordered set (Harris-Michael linked list). Nodes are logically
    /// deleted by marking their next pointer and physically unlinked later,
    /// either by an erasing thread or by any traversing thread. Reclaimed via
    /// hazard pointers (hazard_ptr.hpp).
    ///
    /// The destructor must not run concurrently with any other operation.
    template<typename T>
    class lockfree_list
    {
    private:
        struct node
        {
            std::optional<T> data;  // empty in the head sentinel
            std::atomic<node*> next;

            node() noexcept
                : next(nullptr)
            {
            }

            explicit node(T value)
                : data(std::move(value)), next(nullptr)
            {
            }
        };

    public:
        lockfree_list()
            : head_(new node())
        {
        }

        lockfree_list(const lockfree_list&) = delete;
        lockfree_list& operator=(const lockfree_list&) = delete;

        ~lockfree_list()
        {
            node* current = head_.load(std::memory_order_relaxed);
            while (current != nullptr)
            {
                node* const next = unmark(current->next.load(std::memory_order_relaxed));
                delete current;
                current = next;
            }
        }

        /// Inserts value, keeping the list sorted. Returns false if an equal
        /// element already exists.
        bool insert(T value)
        {
            hp_owner hp_pred;
            hp_owner hp_curr;

            // The key lives inside the node so retries never see a
            // moved-from value; the same node is reused across CAS failures.
            node* const new_node = new node(std::move(value));

            for (;;)
            {
                node* pred = nullptr;
                node* curr = nullptr;

                if (find(*new_node->data, pred, curr, hp_pred.get_pointer(),
                         hp_curr.get_pointer()))
                {
                    delete new_node;
                    return false;
                }

                new_node->next.store(curr, std::memory_order_relaxed);

                node* expected = curr;
                if (pred->next.compare_exchange_strong(expected, new_node,
                                                       std::memory_order_acq_rel,
                                                       std::memory_order_acquire))
                {
                    return true;
                }
            }
        }

        /// Removes the element equal to value. Returns false if not present.
        bool erase(const T& value)
        {
            hp_owner hp_pred;
            hp_owner hp_curr;

            for (;;)
            {
                node* pred = nullptr;
                node* curr = nullptr;

                if (!find(value, pred, curr, hp_pred.get_pointer(), hp_curr.get_pointer()))
                {
                    return false;
                }

                // Logical deletion: mark curr's next pointer.
                node* const next = unmark(curr->next.load(std::memory_order_acquire));
                node* expected = next;
                if (!curr->next.compare_exchange_strong(expected, mark(next),
                                                        std::memory_order_acq_rel,
                                                        std::memory_order_acquire))
                {
                    continue;
                }

                // Best-effort physical unlink; otherwise find() cleans up.
                node* expected_curr = curr;
                if (pred->next.compare_exchange_strong(expected_curr, next,
                                                       std::memory_order_acq_rel,
                                                       std::memory_order_acquire))
                {
                    reclaim_later(curr);
                }

                return true;
            }
        }

        /// Returns true if an element equal to value is present. Also unlinks
        /// logically deleted nodes it traverses, hence non-const.
        bool contains(const T& value)
        {
            hp_owner hp_pred;
            hp_owner hp_curr;
            node* pred = nullptr;
            node* curr = nullptr;
            return find(value, pred, curr, hp_pred.get_pointer(), hp_curr.get_pointer());
        }

        /// Point-in-time approximation under concurrent modification.
        bool empty() const
        {
            node* const sentinel = head_.load(std::memory_order_acquire);
            return unmark(sentinel->next.load(std::memory_order_acquire)) == nullptr;
        }

        /// Visits live elements in ascending order. Test/debug aid: must not
        /// run concurrently with modifications.
        template<typename F>
        void for_each(F&& func)
        {
            node* curr = unmark(head_.load(std::memory_order_relaxed)->next.load(
                std::memory_order_relaxed));

            while (curr != nullptr)
            {
                node* const raw_next = curr->next.load(std::memory_order_relaxed);
                if (!is_marked(raw_next))
                {
                    func(*curr->data);
                }
                curr = unmark(raw_next);
            }
        }

    private:
        static node* mark(node* p) noexcept
        {
            return reinterpret_cast<node*>(reinterpret_cast<std::uintptr_t>(p) | 1u);
        }

        static bool is_marked(node* p) noexcept
        {
            return (reinterpret_cast<std::uintptr_t>(p) & 1u) != 0u;
        }

        static node* unmark(node* p) noexcept
        {
            return reinterpret_cast<node*>(reinterpret_cast<std::uintptr_t>(p)
                                           & ~std::uintptr_t(1u));
        }

        // Positions pred/curr on the first unmarked node with data >= key and
        // its unmarked predecessor, physically unlinking marked nodes on the
        // way. On return both are protected by the caller's hazard slots.
        // Returns true iff curr holds an element equal to key.
        bool find(const T& key, node*& pred_out, node*& curr_out,
                  std::atomic<void*>& hp_pred_ref, std::atomic<void*>& hp_curr_ref)
        {
            std::atomic<void*>* hp_pred = &hp_pred_ref;
            std::atomic<void*>* hp_curr = &hp_curr_ref;

        retry:
            node* pred = head_.load(std::memory_order_acquire);
            hp_pred->store(pred, std::memory_order_release);
            node* curr = unmark(pred->next.load(std::memory_order_acquire));

            for (;;)
            {
                if (curr != nullptr)
                {
                    hp_curr->store(curr, std::memory_order_release);

                    // Validate curr is still the successor of pred before
                    // dereferencing it further.
                    if (unmark(pred->next.load(std::memory_order_acquire)) != curr)
                    {
                        goto retry;
                    }
                }

                node* const raw_next =
                    (curr != nullptr) ? curr->next.load(std::memory_order_acquire) : nullptr;
                node* const next = unmark(raw_next);

                if (curr != nullptr && is_marked(raw_next))
                {
                    // curr is logically deleted: try to unlink it physically.
                    node* expected = curr;
                    if (!pred->next.compare_exchange_strong(expected, next,
                                                            std::memory_order_acq_rel,
                                                            std::memory_order_acquire))
                    {
                        goto retry;
                    }

                    reclaim_later(curr);
                    curr = next;
                    continue;
                }

                if (curr == nullptr || !(*curr->data < key))
                {
                    pred_out = pred;
                    curr_out = curr;
                    return curr != nullptr && !(key < *curr->data) && !(*curr->data < key);
                }

                // Advance: the slot protecting curr becomes the pred slot, so
                // the new pred stays protected; the stale pred slot is reused
                // for the next curr.
                pred = curr;
                curr = next;
                std::swap(hp_pred, hp_curr);
            }
        }

        std::atomic<node*> head_;
    };
}
