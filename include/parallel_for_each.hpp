#pragma once

#include "bonostlpch.h"
#include "thread_pool.hpp"

namespace Bonostl
{
    /// Parallel for_each running on the shared default thread pool
    /// (C++ Concurrency in Action, listing 9.9). While waiting for the first
    /// half, the calling thread helps the pool run pending tasks, so even a
    /// pool narrower than the recursion width cannot deadlock.
    ///
    /// `depth` (default 8) bounds the divide-and-conquer recursion so the
    /// wait-while-helping loop cannot nest unboundedly on the calling
    /// thread's stack for large ranges: the task count is capped at 2^depth
    /// regardless of input size.
    template<typename Iterator, typename Func>
    void parallel_for_each(Iterator first, Iterator last, Func func, unsigned depth = 8)
    {
        static_assert(std::random_access_iterator<Iterator>,
                      "parallel_for_each requires a random-access iterator");

        unsigned long const length = std::distance(first, last);

        if (length == 0)
        {
            return;
        }

        constexpr unsigned long min_per_thread = 25;

        if (length < (2 * min_per_thread) || depth == 0)
        {
            std::ranges::for_each(first, last, func);
        }
        else [[likely]]
        {
            Iterator const mid_point = first + length / 2;
            thread_pool& pool = default_thread_pool();

            std::future<void> first_half = pool.submit([first, mid_point, func, depth] {
                parallel_for_each(first, mid_point, func, depth - 1);
            });

            parallel_for_each(mid_point, last, func, depth - 1);

            while (first_half.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            {
                pool.run_pending_task();
            }

            first_half.get();
        }
    }
}
