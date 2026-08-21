#pragma once

#include "bonostlpch.h"
#include "thread_pool.hpp"

namespace Bonostl
{
    template<typename T>
    struct sorter
    {
        // `depth` bounds the recursion so the wait-while-helping loop cannot
        // overflow the calling thread's stack on large inputs; at depth 0 the
        // remaining chunk is sorted sequentially.
        std::list<T> do_sort(std::list<T> chunk_data, unsigned depth = 8)
        {
            if (chunk_data.empty())
            {
                return chunk_data;
            }

            if (depth == 0)
            {
                chunk_data.sort();
                return chunk_data;
            }

            std::list<T> result;
            result.splice(result.begin(), chunk_data, chunk_data.begin());

            T& partition_val = *result.begin();

            auto const divide_point = std::partition(chunk_data.begin(), chunk_data.end(),
                                                     [&](T const& val) { return val < partition_val; });

            if (divide_point == chunk_data.begin())
            {
                chunk_data.sort();
                result.splice(result.end(), chunk_data);
                return result;
            }

            std::list<T> new_lower_chunk;
            new_lower_chunk.splice(
                new_lower_chunk.end(), chunk_data, chunk_data.begin(), divide_point);

            if (chunk_data.empty())
            {
                new_lower_chunk.sort();
                result.splice(result.begin(), new_lower_chunk);
                return result;
            }

            thread_pool& pool = default_thread_pool();
            std::future<std::list<T>> new_lower = pool.submit(
                [this, new_lower_chunk = std::move(new_lower_chunk), depth]() mutable {
                    return do_sort(std::move(new_lower_chunk), depth - 1);
                });

            std::list<T> new_higher(do_sort(std::move(chunk_data), depth - 1));
            result.splice(result.end(), new_higher);

            while (new_lower.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            {
                pool.run_pending_task();
            }

            result.splice(result.begin(), new_lower.get());
            return result;
        }
    };

    /// Parallel quicksort now backed by the shared default thread pool
    /// (C++ Concurrency in Action, listing 9.9). While waiting for the lower
    /// half, the calling thread helps the pool run pending tasks, so even a
    /// pool narrower than the recursion width cannot deadlock.
    template<typename T>
    std::list<T> parallel_quick_sort(std::list<T> input)
    {
        if (input.empty())
        {
            return input;
        }

        sorter<T> s;
        return s.do_sort(std::move(input));
    }
}