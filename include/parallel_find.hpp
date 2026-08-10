#pragma once

#include "bonostlpch.h"

namespace Bonostl
{
    template<typename Iterator, typename MatchType>
    Iterator parallel_find_impl(Iterator first, Iterator last, MatchType match, std::atomic<bool>& done)
    {
        try
        {
            unsigned long const length = std::distance(first, last);
            constexpr unsigned long min_per_thread = 25;

            if (length < (2 * min_per_thread))
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
                std::future<Iterator> async_result = std::async([&] {
                    return parallel_find_impl(mid_point, last, match, done);
                });

                Iterator const direct_result = parallel_find_impl(first, mid_point, match, done);

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

        return parallel_find_impl(first, last, match, done);
    }
}
