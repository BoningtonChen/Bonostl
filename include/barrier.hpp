#pragma once

#include "bonostlpch.h"

namespace Bonostl
{
    /// Phase barrier. Educational counterpart of std::barrier (C++20); prefer
    /// the std version in production code. Each thread arrives and waits until
    /// all expected threads arrive; an optional completion function then runs
    /// once and the next phase begins.
    class barrier
    {
    public:
        explicit barrier(std::ptrdiff_t expected,
                         std::move_only_function<void()> completion = {})
            : expected_(expected), count_(expected), completion_(std::move(completion))
        {
        }

        barrier(const barrier&) = delete;
        barrier& operator=(const barrier&) = delete;

        void arrive_and_wait()
        {
            std::unique_lock lock(mutex_);
            std::size_t const phase = generation_;

            if (--count_ == 0)
            {
                complete_phase();
                lock.unlock();
                arrived_.notify_all();
            }
            else
            {
                arrived_.wait(lock, [&] { return generation_ != phase; });
            }
        }

        /// Arrive and drop out of future phases (cf. std::barrier::arrive_and_drop).
        void arrive_and_drop()
        {
            std::unique_lock lock(mutex_);
            --expected_;

            if (--count_ == 0)
            {
                complete_phase();
                lock.unlock();
                arrived_.notify_all();
            }
        }

    private:
        void complete_phase()
        {
            if (completion_)
            {
                completion_();
            }

            count_ = expected_;
            ++generation_;
        }

        std::mutex mutex_;
        std::condition_variable arrived_;
        std::ptrdiff_t expected_;
        std::ptrdiff_t count_;
        std::size_t generation_ = 0;
        std::move_only_function<void()> completion_;
    };
}
