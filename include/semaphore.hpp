#pragma once

#include "bonostlpch.h"

namespace Bonostl
{
    /// Counting semaphore. Educational counterpart of std::counting_semaphore
    /// (C++20); prefer the std version in production code.
    class counting_semaphore
    {
    public:
        explicit counting_semaphore(std::ptrdiff_t initial)
            : count_(initial)
        {
        }

        counting_semaphore(const counting_semaphore&) = delete;
        counting_semaphore& operator=(const counting_semaphore&) = delete;

        void acquire()
        {
            std::unique_lock lock(mutex_);
            not_empty_.wait(lock, [&] { return count_ > 0; });
            --count_;
        }

        [[nodiscard]] bool try_acquire()
        {
            std::lock_guard lock(mutex_);

            if (count_ <= 0)
            {
                return false;
            }

            --count_;
            return true;
        }

        void release(std::ptrdiff_t count = 1)
        {
            {
                std::lock_guard lock(mutex_);
                count_ += count;
            }

            for (std::ptrdiff_t i = 0; i < count; ++i)
            {
                not_empty_.notify_one();
            }
        }

    private:
        std::mutex mutex_;
        std::condition_variable not_empty_;
        std::ptrdiff_t count_;
    };

    /// Binary semaphore: at most one permit. Educational counterpart of
    /// std::binary_semaphore. Releasing an already-available semaphore is a
    /// logic error on the caller side (as with the std version).
    class binary_semaphore
    {
    public:
        explicit binary_semaphore(bool initially_available = false)
            : semaphore_(initially_available ? 1 : 0)
        {
        }

        void acquire()
        {
            semaphore_.acquire();
        }

        [[nodiscard]] bool try_acquire()
        {
            return semaphore_.try_acquire();
        }

        void release()
        {
            semaphore_.release(1);
        }

    private:
        counting_semaphore semaphore_;
    };
}
