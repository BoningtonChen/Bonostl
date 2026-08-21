#pragma once

#include "bonostlpch.h"

namespace Bonostl
{
    /// Sequence lock for read-mostly shared data. Writers serialize through an
    /// internal mutex and bump a sequence counter; readers never block — they
    /// read an optimistic snapshot and retry if a write overlapped.
    ///
    /// Usage:
    ///   writer: { seqlock::write_guard guard(lock); /* mutate data */ }
    ///   reader: unsigned seq; do { seq = lock.read_begin(); /* copy data */ }
    ///           while (lock.read_retry(seq));
    ///
    /// The protected data must be safe to copy while torn (e.g. POD structs);
    /// never read pointers through a seqlock without validating the read.
    class seqlock
    {
    public:
        seqlock() = default;

        seqlock(const seqlock&) = delete;
        seqlock& operator=(const seqlock&) = delete;

        /// RAII exclusive writer guard.
        class write_guard
        {
        public:
            explicit write_guard(seqlock& lock)
                : lock_(lock)
            {
                lock_.write_mutex_.lock();
                // Odd sequence marks a write in progress.
                lock_.sequence_.fetch_add(1, std::memory_order_release);
            }

            ~write_guard()
            {
                // Even sequence marks a stable snapshot.
                lock_.sequence_.fetch_add(1, std::memory_order_release);
                lock_.write_mutex_.unlock();
            }

            write_guard(const write_guard&) = delete;
            write_guard& operator=(const write_guard&) = delete;

        private:
            seqlock& lock_;
        };

        /// Returns the current sequence once it is even (no write in progress).
        unsigned read_begin() const noexcept
        {
            unsigned seq;
            do
            {
                seq = sequence_.load(std::memory_order_acquire);
            } while ((seq & 1u) != 0u);

            return seq;
        }

        /// Returns true if no write overlapped the read started at `start`.
        bool read_retry(unsigned start) const noexcept
        {
            std::atomic_thread_fence(std::memory_order_acquire);
            return sequence_.load(std::memory_order_acquire) == start;
        }

    private:
        std::atomic<unsigned> sequence_{0};
        std::mutex write_mutex_;
    };
}
