#pragma once

#include "bonostlpch.h"
#include "epoch_reclaim.hpp"

namespace Bonostl
{
    /// Concurrent sorted set as a lazy skip list (Herlihy & Shavit, The Art of
    /// Multiprocessor Programming, ch. 14). Writers take per-node locks;
    /// readers traverse lock-free. Memory safety uses epoch-based reclamation
    /// (epoch_reclaim.hpp): a whole-operation guard keeps every observable
    /// node alive, which suits a multi-level structure where one operation
    /// touches many raw nodes (hazard pointers protect only O(1) pointers).
    ///
    /// Deletion is lazy: a node is first marked (logical deletion), then
    /// spliced out level by level. The destructor must not run concurrently
    /// with any other operation.
    template<typename T>
    class concurrent_skip_list
    {
    public:
        static constexpr int max_level = 32;

        concurrent_skip_list()
        {
            head_ = new node(max_level + 1, sentinel::head);
            tail_ = new node(max_level + 1, sentinel::tail);
            for (int l = 0; l <= max_level; ++l)
            {
                head_->next[l].store(tail_, std::memory_order_relaxed);
            }
        }

        concurrent_skip_list(const concurrent_skip_list&) = delete;
        concurrent_skip_list& operator=(const concurrent_skip_list&) = delete;

        ~concurrent_skip_list()
        {
            node* current = head_;
            while (current != nullptr)
            {
                node* const next = current->next[0].load(std::memory_order_relaxed);
                delete current;
                current = next;
            }
        }

        /// Inserts value, keeping the set sorted. Returns false if an equal
        /// element already exists.
        bool insert(T value)
        {
            epoch_domain::guard op_guard(domain_);

            int const top = random_level();
            node* const new_node = new node(std::move(value), top);
            node* preds[max_level + 1];
            node* succs[max_level + 1];

            for (;;)
            {
                node* const found = find(*new_node->data, preds, succs);

                if (found != nullptr)
                {
                    if (found->marked.load(std::memory_order_acquire))
                    {
                        continue;  // being deleted by another thread; retry
                    }

                    // Another insert may still be linking it; wait it out.
                    while (!found->fully_linked.load(std::memory_order_acquire))
                    {
                        std::this_thread::yield();
                    }

                    delete new_node;
                    return false;
                }

                for (int l = 0; l <= top; ++l)
                {
                    new_node->next[l].store(succs[l], std::memory_order_relaxed);
                }

                int highest_locked = -1;
                bool valid = true;
                for (int l = 0; l <= top && valid; ++l)
                {
                    // The same pred may span several levels (duplicates are
                    // contiguous); std::mutex is non-recursive, so lock each
                    // distinct node only once.
                    if (l == 0 || preds[l] != preds[l - 1])
                    {
                        preds[l]->mutex.lock();
                    }
                    highest_locked = l;
                    node* const succ = succs[l];
                    valid = !preds[l]->marked.load(std::memory_order_acquire)
                        && !succ->marked.load(std::memory_order_acquire)
                        && preds[l]->next[l].load(std::memory_order_acquire) == succ;
                }

                if (!valid)
                {
                    unlock_preds(preds, highest_locked);
                    continue;  // retry with the same node
                }

                for (int l = 0; l <= top; ++l)
                {
                    preds[l]->next[l].store(new_node, std::memory_order_release);
                }
                new_node->fully_linked.store(true, std::memory_order_release);

                unlock_preds(preds, highest_locked);
                return true;
            }
        }

        /// Removes the element equal to value. Returns false if not present.
        bool erase(const T& value)
        {
            epoch_domain::guard op_guard(domain_);

            node* preds[max_level + 1];
            node* succs[max_level + 1];
            node* victim = nullptr;
            bool is_marked = false;
            int top = -1;

            for (;;)
            {
                node* const found = find(value, preds, succs);

                if (!is_marked)
                {
                    if (found == nullptr)
                    {
                        return false;
                    }

                    victim = found;

                    if (!victim->fully_linked.load(std::memory_order_acquire))
                    {
                        continue;  // being inserted; retry for a stable view
                    }
                    if (victim->marked.load(std::memory_order_acquire))
                    {
                        return false;  // another thread is deleting it
                    }

                    top = victim->top_level;

                    victim->mutex.lock();
                    if (victim->marked.load(std::memory_order_acquire))
                    {
                        victim->mutex.unlock();
                        return false;
                    }
                    // Logical deletion point.
                    victim->marked.store(true, std::memory_order_release);
                    victim->mutex.unlock();
                    is_marked = true;
                }

                // Splice the victim level by level: re-locate its predecessor
                // at each level and unlink under that predecessor's lock.
                // Only one lock is held at a time (no lock ordering issues);
                // a lost race simply re-searches, and the loop exits once the
                // level no longer contains the victim.
                for (int l = top; l >= 0; --l)
                {
                    for (;;)
                    {
                        node* pred = head_;
                        node* curr = pred->next[l].load(std::memory_order_acquire);

                        while (curr != victim && !curr->is_tail() && *curr->data < value)
                        {
                            pred = curr;
                            curr = curr->next[l].load(std::memory_order_acquire);
                        }

                        if (curr != victim)
                        {
                            break;  // already spliced at this level
                        }

                        pred->mutex.lock();
                        if (!pred->marked.load(std::memory_order_acquire)
                            && pred->next[l].load(std::memory_order_acquire) == victim)
                        {
                            pred->next[l].store(
                                victim->next[l].load(std::memory_order_acquire),
                                std::memory_order_release);
                            pred->mutex.unlock();
                            break;
                        }
                        pred->mutex.unlock();
                    }
                }

                domain_.retire(victim);
                return true;
            }
        }

        /// Returns true if an element equal to value is present.
        bool contains(const T& value)
        {
            epoch_domain::guard op_guard(domain_);

            node* preds[max_level + 1];
            node* succs[max_level + 1];

            node* const found = find(value, preds, succs);
            return found != nullptr
                && found->fully_linked.load(std::memory_order_acquire)
                && !found->marked.load(std::memory_order_acquire);
        }

        /// Point-in-time approximation under concurrent modification.
        bool empty() const
        {
            return head_->next[0].load(std::memory_order_acquire) == tail_;
        }

        /// Visits live elements in ascending order. Test/debug aid: must not
        /// run concurrently with modifications.
        template<typename F>
        void for_each(F&& func)
        {
            node* curr = head_->next[0].load(std::memory_order_relaxed);
            while (!curr->is_tail())
            {
                if (curr->fully_linked.load(std::memory_order_relaxed)
                    && !curr->marked.load(std::memory_order_relaxed))
                {
                    func(*curr->data);
                }
                curr = curr->next[0].load(std::memory_order_relaxed);
            }
        }

    private:
        enum class sentinel
        {
            head,
            tail,
            none
        };

        struct node
        {
            std::optional<T> data;  // empty in the head/tail sentinels
            int const top_level;
            std::unique_ptr<std::atomic<node*>[]> next;  // top_level + 1 entries
            std::mutex mutex;
            std::atomic<bool> marked{false};
            std::atomic<bool> fully_linked{false};
            sentinel const kind;

            // Sentinel constructor (levels entries, all null).
            node(int levels, sentinel kind_)
                : top_level(levels - 1),
                  next(std::make_unique<std::atomic<node*>[]>(levels)),
                  kind(kind_)
            {
                for (int l = 0; l < levels; ++l)
                {
                    next[l].store(nullptr, std::memory_order_relaxed);
                }
            }

            // Data node.
            node(T value, int level)
                : data(std::move(value)),
                  top_level(level),
                  next(std::make_unique<std::atomic<node*>[]>(level + 1)),
                  kind(sentinel::none)
            {
                for (int l = 0; l <= level; ++l)
                {
                    next[l].store(nullptr, std::memory_order_relaxed);
                }
            }

            bool is_tail() const noexcept
            {
                return kind == sentinel::tail;
            }
        };

        static int random_level()
        {
            thread_local std::mt19937 rng(std::random_device{}());

            int level = 0;
            while (level < max_level && (rng() & 1u) != 0u)
            {
                ++level;
            }
            return level;
        }

        // Unlocks each distinct pred up to highest_locked (the same node may
        // be locked for several contiguous levels but only once).
        static void unlock_preds(node* const* preds, int highest_locked)
        {
            for (int l = highest_locked; l >= 0; --l)
            {
                if (l == 0 || preds[l] != preds[l - 1])
                {
                    preds[l]->mutex.unlock();
                }
            }
        }

        // Fills preds/succs for every level and returns the level-0 node whose
        // data == key (regardless of its marked/fully_linked state), or
        // nullptr if absent. Lock-free raw-pointer traversal; memory safety is
        // provided by the caller's epoch guard, which keeps every reachable
        // node alive for the whole operation.
        node* find(const T& key, node** preds, node** succs)
        {
            node* pred = head_;

            for (int level = max_level; level >= 0; --level)
            {
                node* curr = pred->next[level].load(std::memory_order_acquire);

                while (!curr->is_tail() && *curr->data < key)
                {
                    pred = curr;
                    curr = curr->next[level].load(std::memory_order_acquire);
                }

                preds[level] = pred;
                succs[level] = curr;
            }

            node* const candidate = succs[0];
            if (!candidate->is_tail() && !(key < *candidate->data)
                && !(*candidate->data < key))
            {
                return candidate;
            }
            return nullptr;
        }

        node* head_;
        node* tail_;
        epoch_domain domain_;
    };
}
