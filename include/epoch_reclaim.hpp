#pragma once

#include "bonostlpch.h"

namespace Bonostl
{
    /// Epoch-based reclamation domain. Each operation wraps its body in a
    /// guard; nodes retired while operations are in flight are reclaimed once
    /// no active operation started before the retirement.
    ///
    /// Complements hazard_ptr.hpp: coarser (whole-operation protection, no
    /// per-pointer choreography) and unbounded pointer counts, at the cost of
    /// reclamation latency. Suited to multi-level structures (skip list)
    /// where many raw nodes are touched per operation.
    class epoch_domain
    {
        struct thread_record;

    public:
        /// RAII operation scope. While a guard is alive, every node the
        /// operation could have observed is guaranteed to stay allocated.
        class guard
        {
        public:
            explicit guard(epoch_domain& domain)
                : domain_(domain), slot_(domain_.claim_slot())
            {
            }

            ~guard()
            {
                domain_.release_slot(slot_);
            }

            guard(const guard&) = delete;
            guard& operator=(const guard&) = delete;

        private:
            epoch_domain& domain_;
            thread_record* slot_;
        };

        epoch_domain() = default;

        epoch_domain(const epoch_domain&) = delete;
        epoch_domain& operator=(const epoch_domain&) = delete;

        /// Retires a node; it is reclaimed automatically once no active guard
        /// announced an epoch after the retirement.
        template<typename T>
        void retire(T* ptr)
        {
            retired_node* const node = new retired_node{
                static_cast<void*>(ptr), &do_delete<T>,
                global_epoch_.load(std::memory_order_relaxed), nullptr};

            push_retired(node);

            if (retire_count_.fetch_add(1, std::memory_order_relaxed) + 1 >= reclaim_threshold)
            {
                collect();
            }
        }

        /// Forces a reclamation pass (also happens automatically every
        /// reclaim_threshold retirements).
        void collect() noexcept
        {
            retire_count_.store(0, std::memory_order_relaxed);

            unsigned long min_announced = std::numeric_limits<unsigned long>::max();
            for (auto& record : registry_)
            {
                if (record.active.load(std::memory_order_acquire))
                {
                    min_announced = std::min(
                        min_announced,
                        record.announced_epoch.load(std::memory_order_acquire));
                }
            }

            retired_node* list = retired_head_.exchange(nullptr, std::memory_order_acq_rel);
            while (list != nullptr)
            {
                retired_node* const next = list->next;
                if (list->retire_epoch < min_announced)
                {
                    delete list;
                }
                else
                {
                    push_retired(list);
                }
                list = next;
            }
        }

    private:
        static constexpr std::size_t max_threads = 128;
        static constexpr unsigned reclaim_threshold = 128;

        struct thread_record
        {
            std::atomic<std::thread::id> owner;
            std::atomic<unsigned long> announced_epoch{0};
            std::atomic<bool> active{false};
        };

        struct retired_node
        {
            void* ptr;
            std::move_only_function<void(void*)> deleter;
            unsigned long retire_epoch;
            retired_node* next;

            ~retired_node()
            {
                deleter(ptr);
            }
        };

        template<typename T>
        static void do_delete(void* p)
        {
            delete static_cast<T*>(p);
        }

        thread_record* claim_slot()
        {
            const std::thread::id self = std::this_thread::get_id();

            for (auto& record : registry_)
            {
                std::thread::id empty;
                if (record.owner.compare_exchange_strong(empty, self,
                                                         std::memory_order_acquire,
                                                         std::memory_order_relaxed))
                {
                    // Every entry announces a fresh, unique epoch.
                    record.announced_epoch.store(
                        global_epoch_.fetch_add(1, std::memory_order_acq_rel) + 1,
                        std::memory_order_release);
                    record.active.store(true, std::memory_order_release);
                    return &record;
                }
            }

            throw std::runtime_error("No epoch slots available");
        }

        void release_slot(thread_record* slot) noexcept
        {
            slot->active.store(false, std::memory_order_release);
            slot->owner.store(std::thread::id(), std::memory_order_release);
        }

        void push_retired(retired_node* node) noexcept
        {
            node->next = retired_head_.load(std::memory_order_acquire);
            while (!retired_head_.compare_exchange_weak(node->next, node,
                                                        std::memory_order_release,
                                                        std::memory_order_relaxed))
            {
            }
        }

        std::array<thread_record, max_threads> registry_;
        std::atomic<unsigned long> global_epoch_{0};
        std::atomic<retired_node*> retired_head_{nullptr};
        std::atomic<unsigned> retire_count_{0};
    };
}
