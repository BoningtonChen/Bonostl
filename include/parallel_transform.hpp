#pragma once

#include "bonostlpch.h"
#include "thread_pool.hpp"

namespace Bonostl
{
    /// Parallel transform running on the shared default thread pool,
    /// mirroring the recursion scheme of parallel_for_each (C++ Concurrency in
    /// Action, listing 9.9): the range is split at its midpoint, the first
    /// half is submitted to the pool, the calling thread processes the second
    /// half and then helps the pool run pending tasks while waiting
    /// (wait-while-helping), so even a pool narrower than the recursion width
    /// cannot deadlock.
    ///
    /// Requirements:
    /// - Iterator and OutIt must be random-access iterators (checked by the
    ///   static_asserts below) so the range can be split at a midpoint.
    /// - func must not mutate shared state: it is invoked concurrently on
    ///   distinct elements, so each application must be independent of the
    ///   others (element-wise independence; no ordering guarantees).
    ///
    /// Returns the output iterator past the last element written, matching the
    /// return semantics of std::transform.
    template<typename Iterator, typename OutIt, typename Func>
    OutIt parallel_transform(Iterator first, Iterator last, OutIt out, Func func)
    {
        static_assert(std::random_access_iterator<Iterator>,
                      "parallel_transform requires a random-access iterator");
        static_assert(std::random_access_iterator<OutIt>,
                      "parallel_transform requires a random-access output iterator");

        unsigned long const length = std::distance(first, last);

        if (length == 0)
        {
            return out;
        }

        constexpr unsigned long min_per_thread = 25;

        if (length < (2 * min_per_thread))
        {
            return std::transform(first, last, out, func);
        }
        else [[likely]]
        {
            Iterator const mid_point = first + length / 2;
            OutIt const mid_out = out + length / 2;
            thread_pool& pool = default_thread_pool();

            std::future<OutIt> first_half = pool.submit([first, mid_point, out, func] {
                return parallel_transform(first, mid_point, out, func);
            });

            OutIt const out_end = parallel_transform(mid_point, last, mid_out, func);

            while (first_half.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            {
                pool.run_pending_task();
            }

            (void)first_half.get(); // propagate exceptions from the first half

            return out_end;
        }
    }
}
