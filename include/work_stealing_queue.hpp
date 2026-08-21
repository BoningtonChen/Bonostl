#pragma once

#include "bonostlpch.h"
#include "function_wrapper.hpp"

namespace Bonostl
{
    /// Work-stealing queue of tasks, used as the per-thread local queue in a
    /// thread pool. The owner pushes and pops from the front (LIFO, cache
    /// friendly); thieves steal from the back (FIFO) to reduce contention.
    class work_stealing_queue
    {
    public:
        work_stealing_queue() = default;

        work_stealing_queue(const work_stealing_queue&) = delete;
        work_stealing_queue& operator=(const work_stealing_queue&) = delete;

        void push(function_wrapper task)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            queue_.push_front(std::move(task));
        }

        bool try_pop(function_wrapper& out)
        {
            std::lock_guard<std::mutex> lock(mutex_);

            if (queue_.empty())
            {
                return false;
            }

            out = std::move(queue_.front());
            queue_.pop_front();
            return true;
        }

        bool try_steal(function_wrapper& out)
        {
            std::lock_guard<std::mutex> lock(mutex_);

            if (queue_.empty())
            {
                return false;
            }

            out = std::move(queue_.back());
            queue_.pop_back();
            return true;
        }

    private:
        std::deque<function_wrapper> queue_;
        std::mutex mutex_;
    };
}
