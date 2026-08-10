#pragma once

#include "bonostlpch.h"

namespace Bonostl
{
    class spinlock_mutex
    {
    public:
        spinlock_mutex() noexcept = default;

        spinlock_mutex(const spinlock_mutex&) = delete;
        spinlock_mutex& operator=(const spinlock_mutex&) = delete;

        void lock() noexcept
        {
            while (flag.test_and_set(std::memory_order_acquire))
            {
                while (flag.test(std::memory_order_relaxed))
                {
                    std::this_thread::yield();
                }
            }
        }

        [[nodiscard]] bool try_lock() noexcept
        {
            return !flag.test_and_set(std::memory_order_acquire);
        }

        void unlock() noexcept
        {
            flag.clear(std::memory_order_release);
        }

    private:
        std::atomic_flag flag;
    };
}
