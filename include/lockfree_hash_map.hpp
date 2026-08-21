#pragma once

#include <cstdint>

#include "bonostlpch.h"
#include "hazard_ptr.hpp"

namespace Bonostl
{
    /// Lock-free hash map as a split-ordered list (Shalev & Shavit): a single
    /// lock-free ordered linked list holds both bucket sentinel nodes and data
    /// nodes, ordered by bit-reversed keys. Data nodes of a bucket sort right
    /// after the bucket's sentinel, so lookups start mid-list at the sentinel
    /// and scan only a short bucket chain. Resizing inserts new sentinels
    /// only — data nodes never move — and bucket sentinels are created lazily
    /// on first access.
    ///
    /// Requirements: Key must be hashable (Hash), ordered (operator<),
    /// default-constructible and copy-constructible; Value must be
    /// copy-constructible. get returns a Value snapshot; put updates an
    /// existing key atomically (readers see either the old or the new value).
    ///
    /// The destructor must not run concurrently with any other operation.
    template<typename Key, typename Value, typename Hash = std::hash<Key>>
    class lockfree_hash_map
    {
    public:
        explicit lockfree_hash_map(std::size_t initial_buckets = 2, Hash hasher = Hash())
            : bucket_count_(normalize_bucket_count(initial_buckets)),
              hasher_(std::move(hasher)),
              head_(new node(bucket_split_key(0)))
        {
        }

        lockfree_hash_map(const lockfree_hash_map&) = delete;
        lockfree_hash_map& operator=(const lockfree_hash_map&) = delete;

        ~lockfree_hash_map()
        {
            node* current = head_;
            while (current != nullptr)
            {
                node* const next = unmark(current->next.load(std::memory_order_relaxed));
                delete current;
                current = next;
            }
        }

        /// Returns a snapshot of the value for key, or nullopt if absent.
        std::optional<Value> get(const Key& key)
        {
            hp_owner hp_pred;
            hp_owner hp_curr;

            node* pred = nullptr;
            node* curr = nullptr;
            if (find_data_node(key, pred, curr, hp_pred.get_pointer(), hp_curr.get_pointer()))
            {
                if (auto snapshot = curr->value.load(std::memory_order_acquire))
                {
                    return std::optional<Value>(*snapshot);
                }
            }
            return std::nullopt;
        }

        /// Inserts key/value; if key exists, atomically replaces the value
        /// snapshot. Returns true if a new node was inserted.
        bool put(Key key, Value value)
        {
            std::size_t const hash = hasher_(key);
            node* const sentinel = find_bucket(bucket_of(hash));
            std::uint64_t const split_key = data_split_key(hash);

            // The key and value live inside the node so retries never see a
            // moved-from state; the same node is reused across CAS failures.
            node* const new_node = new node(split_key, std::move(key), std::move(value));

            hp_owner hp_pred;
            hp_owner hp_curr;

            for (;;)
            {
                node* pred = nullptr;
                node* curr = nullptr;

                if (find_from(sentinel, split_key, *new_node->key, pred, curr,
                              hp_pred.get_pointer(), hp_curr.get_pointer()))
                {
                    // Key exists: swap the value snapshot in place.
                    curr->value.store(new_node->value.load(std::memory_order_acquire),
                                      std::memory_order_release);
                    delete new_node;
                    return false;
                }

                new_node->next.store(curr, std::memory_order_relaxed);

                node* expected = curr;
                if (pred->next.compare_exchange_strong(expected, new_node,
                                                       std::memory_order_acq_rel,
                                                       std::memory_order_acquire))
                {
                    if (size_.fetch_add(1, std::memory_order_relaxed) + 1 > bucket_count())
                    {
                        grow();
                    }
                    return true;
                }
            }
        }

        /// Removes key. Returns false if absent.
        bool erase(const Key& key)
        {
            hp_owner hp_pred;
            hp_owner hp_curr;

            for (;;)
            {
                node* pred = nullptr;
                node* curr = nullptr;

                if (!find_data_node(key, pred, curr, hp_pred.get_pointer(),
                                    hp_curr.get_pointer()))
                {
                    return false;
                }

                node* const next = unmark(curr->next.load(std::memory_order_acquire));
                node* expected = next;
                if (!curr->next.compare_exchange_strong(expected, mark(next),
                                                        std::memory_order_acq_rel,
                                                        std::memory_order_acquire))
                {
                    continue;
                }

                node* expected_curr = curr;
                if (pred->next.compare_exchange_strong(expected_curr, next,
                                                       std::memory_order_acq_rel,
                                                       std::memory_order_acquire))
                {
                    reclaim_later(curr);
                }

                size_.fetch_sub(1, std::memory_order_relaxed);
                return true;
            }
        }

        bool contains(const Key& key)
        {
            hp_owner hp_pred;
            hp_owner hp_curr;
            node* pred = nullptr;
            node* curr = nullptr;
            return find_data_node(key, pred, curr, hp_pred.get_pointer(),
                                  hp_curr.get_pointer());
        }

        /// Approximate under concurrent modification.
        std::size_t size() const noexcept
        {
            return size_.load(std::memory_order_relaxed);
        }

        std::size_t bucket_count() const noexcept
        {
            return bucket_count_.load(std::memory_order_acquire);
        }

    private:
        struct node
        {
            std::uint64_t const split_key;
            std::optional<Key> const key;  // nullopt marks a bucket sentinel
            std::atomic<std::shared_ptr<Value>> value;
            std::atomic<node*> next;

            // Bucket sentinel.
            explicit node(std::uint64_t split_key_)
                : split_key(split_key_), key(std::nullopt), value(nullptr), next(nullptr)
            {
            }

            // Data node.
            node(std::uint64_t split_key_, Key key_, Value value_)
                : split_key(split_key_),
                  key(std::move(key_)),
                  value(std::make_shared<Value>(std::move(value_))),
                  next(nullptr)
            {
            }
        };

        static std::size_t normalize_bucket_count(std::size_t n) noexcept
        {
            std::size_t count = 1;
            while (count < n)
            {
                count <<= 1;
            }
            return count;
        }

        // Bit-reversed 32-bit value: the low bits of the hash (the bucket
        // index) become the high bits of the ordering key, clustering each
        // bucket's data nodes behind its sentinel.
        static std::uint32_t reverse_bits(std::uint32_t v) noexcept
        {
            v = ((v >> 1) & 0x55555555u) | ((v & 0x55555555u) << 1);
            v = ((v >> 2) & 0x33333333u) | ((v & 0x33333333u) << 2);
            v = ((v >> 4) & 0x0F0F0F0Fu) | ((v & 0x0F0F0F0Fu) << 4);
            v = ((v >> 8) & 0x00FF00FFu) | ((v & 0x00FF00FFu) << 8);
            return (v >> 16) | (v << 16);
        }

        // Sentinels get an even split key, data nodes the odd key of the same
        // hash, so a bucket sentinel always sorts before its members.
        static std::uint64_t bucket_split_key(std::size_t bucket) noexcept
        {
            return static_cast<std::uint64_t>(
                       reverse_bits(static_cast<std::uint32_t>(bucket)))
                   << 1;
        }

        static std::uint64_t data_split_key(std::size_t hash) noexcept
        {
            return (static_cast<std::uint64_t>(
                        reverse_bits(static_cast<std::uint32_t>(hash)))
                    << 1)
                | 1u;
        }

        std::size_t bucket_of(std::size_t hash) const noexcept
        {
            return hash & (bucket_count() - 1);
        }

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

        // Harris-Michael find starting at `start` (a live sentinel, never
        // reclaimed): positions pred/curr around (split_key, key), physically
        // unlinking marked nodes on the way. Returns true iff curr is a data
        // node equal to (split_key, key). On return both are hazard-protected
        // by the caller's slots.
        bool find_from(node* start, std::uint64_t split_key, const Key& key, node*& pred_out,
                       node*& curr_out, std::atomic<void*>& hp_pred_ref,
                       std::atomic<void*>& hp_curr_ref)
        {
            std::atomic<void*>* hp_pred = &hp_pred_ref;
            std::atomic<void*>* hp_curr = &hp_curr_ref;

        retry:
            node* pred = start;
            hp_pred->store(pred, std::memory_order_release);
            node* curr = unmark(pred->next.load(std::memory_order_acquire));

            for (;;)
            {
                if (curr != nullptr)
                {
                    hp_curr->store(curr, std::memory_order_release);

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
                    // Only data nodes are reclaimed; sentinels stay forever.
                    node* expected = curr;
                    if (!pred->next.compare_exchange_strong(expected, next,
                                                            std::memory_order_acq_rel,
                                                            std::memory_order_acquire))
                    {
                        goto retry;
                    }
                    if (curr->key.has_value())
                    {
                        reclaim_later(curr);
                    }
                    curr = next;
                    continue;
                }

                if (curr == nullptr || curr->split_key > split_key
                    || (curr->split_key == split_key
                        && (!curr->key.has_value() || !(*curr->key < key))))
                {
                    pred_out = pred;
                    curr_out = curr;
                    return curr != nullptr && curr->split_key == split_key
                        && curr->key.has_value() && !(key < *curr->key)
                        && !(*curr->key < key);
                }

                pred = curr;
                curr = next;
                std::swap(hp_pred, hp_curr);
            }
        }

        // Same-key lookup from the bucket sentinel. hp slots come from the
        // caller and stay valid until it returns.
        bool find_data_node(const Key& key, node*& pred_out, node*& curr_out,
                            std::atomic<void*>& hp_pred, std::atomic<void*>& hp_curr)
        {
            std::size_t const hash = hasher_(key);
            node* const sentinel = find_bucket(bucket_of(hash));
            return find_from(sentinel, data_split_key(hash), key, pred_out, curr_out, hp_pred,
                             hp_curr);
        }

        // Returns the sentinel node of bucket b, creating it (and any missing
        // ancestors, recursively) if needed. Sentinels are never reclaimed.
        node* find_bucket(std::size_t bucket)
        {
            std::uint64_t const split_key = bucket_split_key(bucket);

            if (bucket == 0)
            {
                return head_;
            }

            hp_owner hp_pred;
            hp_owner hp_curr;
            node* pred = nullptr;
            node* curr = nullptr;

            for (;;)
            {
                if (find_sentinel(split_key, pred, curr, hp_pred.get_pointer(),
                                  hp_curr.get_pointer()))
                {
                    return curr;
                }

                // Missing: create it. The parent bucket's sentinel is the
                // insertion anchor. The parent is the bucket with its most
                // significant set bit cleared — generation-consistent
                // regardless of the current bucket count (buckets split in
                // power-of-two order and are created lazily on first access).
                std::size_t const parent = bucket ^ std::bit_floor(bucket);
                node* const parent_sentinel = find_bucket(parent);

                node* const sentinel = new node(split_key);
                Key unused_key{};  // sentinels carry no key

                find_from(parent_sentinel, split_key, unused_key, pred, curr,
                          hp_pred.get_pointer(), hp_curr.get_pointer());

                sentinel->next.store(curr, std::memory_order_relaxed);
                node* expected = curr;
                if (pred->next.compare_exchange_strong(expected, sentinel,
                                                       std::memory_order_acq_rel,
                                                       std::memory_order_acquire))
                {
                    return sentinel;
                }

                // Lost the race (a sentinel or a data node was inserted
                // concurrently): retry the search rather than return a data
                // node as if it were the sentinel.
                delete sentinel;
            }
        }

        // Positions pred/curr around the bucket sentinel with the given even
        // split key and reports whether the sentinel was found. Sentinel
        // detection cannot reuse find_from's return value (its "found"
        // semantics cover data nodes only; a sentinel has no key).
        bool find_sentinel(std::uint64_t split_key, node*& pred_out, node*& curr_out,
                           std::atomic<void*>& hp_pred, std::atomic<void*>& hp_curr)
        {
            Key unused_key{};
            find_from(head_, split_key, unused_key, pred_out, curr_out, hp_pred, hp_curr);
            return curr_out != nullptr && curr_out->split_key == split_key
                && !curr_out->key.has_value();
        }

        void grow()
        {
            std::size_t count = bucket_count();
            if (size_.load(std::memory_order_relaxed) <= count)
            {
                return;
            }
            // Double the bucket count; new sentinels are created lazily by
            // find_bucket on next access.
            bucket_count_.compare_exchange_strong(count, count * 2,
                                                  std::memory_order_acq_rel,
                                                  std::memory_order_acquire);
        }

        std::atomic<std::size_t> bucket_count_;
        Hash hasher_;
        node* const head_;
        std::atomic<std::size_t> size_{0};
    };
}
