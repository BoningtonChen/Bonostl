#pragma once

#include "bonostlpch.h"

namespace Bonostl
{
    /// Single-use countdown latch. Educational counterpart of std::latch
    /// (C++20); prefer the std version in production code.
    class latch
    {
    public:
        explicit latch(std::ptrdiff_t expected)
            : count_(expected)
        {
        }

        latch(const latch&) = delete;
        latch& operator=(const latch&) = delete;

        void count_down(std::ptrdiff_t n = 1)
        {
            std::lock_guard lock(mutex_);
            count_ -= n;

            if (count_ <= 0)
            {
                zero_.notify_all();
            }
        }

        [[nodiscard]] bool try_wait() const
        {
            std::lock_guard lock(mutex_);
            return count_ <= 0;
        }

        void wait() const
        {
            std::unique_lock lock(mutex_);
            zero_.wait(lock, [&] { return count_ <= 0; });
        }

        void arrive_and_wait(std::ptrdiff_t n = 1)
        {
            count_down(n);
            wait();
        }

    private:
        mutable std::mutex mutex_;
        mutable std::condition_variable zero_;
        std::ptrdiff_t count_;
    };
}
