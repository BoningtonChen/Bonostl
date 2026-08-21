#pragma once

#include "bonostlpch.h"
#include "thread_pool.hpp"

namespace Bonostl
{
    /// Parallel stable merge sort on the shared default thread pool. The two
    /// halves are sorted concurrently (one on the pool, one on the calling
    /// thread, with wait-while-helping) and merged with std::inplace_merge —
    /// the merge itself is sequential in this version.
    template<typename Iterator>
    void parallel_merge_sort(Iterator first, Iterator last)
    {
        static_assert(std::random_access_iterator<Iterator>,
                      "parallel_merge_sort requires a random-access iterator");

        unsigned long const length = std::distance(first, last);

        constexpr unsigned long sequential_threshold = 512;

        if (length < sequential_threshold)
        {
            std::stable_sort(first, last);
            return;
        }

        Iterator const mid_point = first + length / 2;
        thread_pool& pool = default_thread_pool();

        std::future<void> first_half = pool.submit([first, mid_point] {
            parallel_merge_sort(first, mid_point);
        });

        parallel_merge_sort(mid_point, last);

        while (first_half.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        {
            pool.run_pending_task();
        }

        first_half.get();
        std::inplace_merge(first, mid_point, last);
    }
}
