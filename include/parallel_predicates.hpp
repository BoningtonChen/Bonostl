#pragma once

#include "bonostlpch.h"
#include "thread_pool.hpp"

namespace Bonostl
{
    namespace detail
    {
        template<typename Iterator, typename Predicate>
        bool parallel_any_of_impl(Iterator first, Iterator last, Predicate pred,
                                  std::atomic<bool>& done)
        {
            unsigned long const length = std::distance(first, last);

            if (length == 0)
            {
                return false;
            }

            constexpr unsigned long min_per_thread = 25;

            if (length < (2 * min_per_thread))
            {
                for (; first != last && !done.load(std::memory_order_relaxed); ++first)
                {
                    if (pred(*first))
                    {
                        done.store(true, std::memory_order_relaxed);
                        return true;
                    }
                }
                return false;
            }
            else [[likely]]
            {
                Iterator const mid_point = first + length / 2;

                if (done.load(std::memory_order_relaxed))
                {
                    // Another branch already found a match; OR-ing with the
                    // finding branch propagates true up the chain.
                    return false;
                }

                thread_pool& pool = default_thread_pool();
                std::future<bool> async_result = pool.submit([mid_point, last, pred, &done] {
                    return parallel_any_of_impl(mid_point, last, pred, done);
                });

                bool const direct_result = parallel_any_of_impl(first, mid_point, pred, done);

                while (async_result.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
                {
                    pool.run_pending_task();
                }

                return async_result.get() || direct_result;
            }
        }
    }

    /// Parallel count_if on the shared default thread pool. Equivalent result
    /// to std::count_if.
    template<typename Iterator, typename Predicate>
    std::size_t parallel_count_if(Iterator first, Iterator last, Predicate pred)
    {
        static_assert(std::random_access_iterator<Iterator>,
                      "parallel_count_if requires a random-access iterator");

        unsigned long const length = std::distance(first, last);

        if (length == 0)
        {
            return 0;
        }

        constexpr unsigned long min_per_thread = 25;

        if (length < (2 * min_per_thread))
        {
            return static_cast<std::size_t>(std::count_if(first, last, pred));
        }
        else [[likely]]
        {
            Iterator const mid_point = first + length / 2;
            thread_pool& pool = default_thread_pool();

            std::future<std::size_t> first_half = pool.submit([first, mid_point, pred] {
                return parallel_count_if(first, mid_point, pred);
            });

            std::size_t const second_half = parallel_count_if(mid_point, last, pred);

            while (first_half.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            {
                pool.run_pending_task();
            }

            return first_half.get() + second_half;
        }
    }

    /// Parallel any_of with early exit: once any branch finds a match, the
    /// remaining branches stop at their next element check.
    template<typename Iterator, typename Predicate>
    bool parallel_any_of(Iterator first, Iterator last, Predicate pred)
    {
        static_assert(std::random_access_iterator<Iterator>,
                      "parallel_any_of requires a random-access iterator");

        std::atomic<bool> done(false);
        return detail::parallel_any_of_impl(first, last, pred, done);
    }

    /// Parallel all_of, expressed as !any_of(!pred) with early exit.
    template<typename Iterator, typename Predicate>
    bool parallel_all_of(Iterator first, Iterator last, Predicate pred)
    {
        static_assert(std::random_access_iterator<Iterator>,
                      "parallel_all_of requires a random-access iterator");

        return !parallel_any_of(first, last, [&pred](auto&& value) {
            return !pred(value);
        });
    }

    /// Parallel none_of, expressed as !any_of(pred) with early exit.
    template<typename Iterator, typename Predicate>
    bool parallel_none_of(Iterator first, Iterator last, Predicate pred)
    {
        static_assert(std::random_access_iterator<Iterator>,
                      "parallel_none_of requires a random-access iterator");

        return !parallel_any_of(first, last, pred);
    }
}
