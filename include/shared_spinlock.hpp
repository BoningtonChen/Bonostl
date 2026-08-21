#pragma once

#include "bonostlpch.h"

namespace Bonostl
{
    /// Reader-writer spin lock for short critical sections. Satisfies
    /// BasicLockable and SharedLockable, so it works with std::lock_guard,
    /// std::unique_lock and std::shared_lock.
    ///
    /// Note: writers can starve under a continuous stream of readers (no
    /// writer preference); use std::shared_mutex when that matters.
    class shared_spinlock
    {
    public:
        shared_spinlock() noexcept = default;

        shared_spinlock(const shared_spinlock&) = delete;
        shared_spinlock& operator=(const shared_spinlock&) = delete;

        void lock() noexcept
        {
            unsigned expected = 0;
            while (!state_.compare_exchange_weak(expected, writer_bit,
                                                 std::memory_order_acquire,
                                                 std::memory_order_relaxed))
            {
                expected = 0;
                std::this_thread::yield();
            }
        }

        [[nodiscard]] bool try_lock() noexcept
        {
            unsigned expected = 0;
            return state_.compare_exchange_strong(expected, writer_bit,
                                                  std::memory_order_acquire,
                                                  std::memory_order_relaxed);
        }

        void unlock() noexcept
        {
            state_.store(0, std::memory_order_release);
        }

        void lock_shared() noexcept
        {
            unsigned observed = state_.load(std::memory_order_relaxed);
            for (;;)
            {
                if ((observed & writer_bit) != 0u)
                {
                    std::this_thread::yield();
                    observed = state_.load(std::memory_order_relaxed);
                    continue;
                }

                // A failed CAS reloads `observed` with the current value.
                if (state_.compare_exchange_weak(observed, observed + 1,
                                                 std::memory_order_acquire,
                                                 std::memory_order_relaxed))
                {
                    return;
                }
            }
        }

        [[nodiscard]] bool try_lock_shared() noexcept
        {
            unsigned observed = state_.load(std::memory_order_relaxed);
            if ((observed & writer_bit) != 0u)
            {
                return false;
            }
            return state_.compare_exchange_strong(observed, observed + 1,
                                                  std::memory_order_acquire,
                                                  std::memory_order_relaxed);
        }

        void unlock_shared() noexcept
        {
            state_.fetch_sub(1, std::memory_order_release);
        }

    private:
        static constexpr unsigned writer_bit = 1u << 31;

        std::atomic<unsigned> state_{0};
    };
}
