#pragma once

#include "bonostlpch.h"
#include "thread_pool.hpp"

namespace Bonostl
{
    template<typename Iterator, typename MatchType>
    Iterator parallel_find_impl(Iterator first, Iterator last, MatchType match,
                                std::atomic<bool>& done, unsigned depth)
    {
        static_assert(std::random_access_iterator<Iterator>,
                      "parallel_find requires a random-access iterator");

        try
        {
            unsigned long const length = std::distance(first, last);
            constexpr unsigned long min_per_thread = 25;

            if (length < (2 * min_per_thread) || depth == 0)
            {
                for (; first != last && !done.load(std::memory_order_relaxed); ++first)
                {
                    if (*first == match)
                    {
                        done.store(true, std::memory_order_relaxed);
                        return first;
                    }
                }
                return last;
            }
            else [[likely]]
            {
                Iterator const mid_point = first + (length / 2);

                if (done.load(std::memory_order_relaxed))
                {
                    return last;
                }

                thread_pool& pool = default_thread_pool();
                std::future<Iterator> async_result =
                    pool.submit([mid_point, last, match, &done, depth] {
                        return parallel_find_impl(mid_point, last, match, done, depth - 1);
                    });

                Iterator const direct_result =
                    parallel_find_impl(first, mid_point, match, done, depth - 1);

                while (async_result.wait_for(std::chrono::seconds(0))
                       != std::future_status::ready)
                {
                    pool.run_pending_task();
                }

                return (direct_result == mid_point) ? async_result.get() : direct_result;
            }
        }
        catch (...)
        {
            done.store(true, std::memory_order_relaxed);
            throw;
        }
    }

    template<typename Iterator, typename MatchType>
    Iterator parallel_find(Iterator first, Iterator last, MatchType match)
    {
        std::atomic<bool> done(false);

        return parallel_find_impl(first, last, match, done, 8);
    }
}
