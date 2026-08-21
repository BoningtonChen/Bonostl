#pragma once

#include "bonostlpch.h"
#include "thread_pool.hpp"

namespace Bonostl
{
    /// Parallel inclusive scan (prefix sum) on the shared default thread pool.
    ///
    /// Three phases: block totals are reduced concurrently, their exclusive
    /// prefix (per-block carries) is computed sequentially, then each block
    /// runs a local std::inclusive_scan seeded with its carry. Blocks are
    /// disjoint, so out may alias first (in-place scan). op must be
    /// associative; the result matches std::inclusive_scan.
    template<typename Iterator, typename OutIt, typename BinaryOp>
    OutIt parallel_inclusive_scan(Iterator first, Iterator last, OutIt out, BinaryOp op)
    {
        static_assert(std::random_access_iterator<Iterator>,
                      "parallel_inclusive_scan requires a random-access iterator");
        static_assert(std::random_access_iterator<OutIt>,
                      "parallel_inclusive_scan requires a random-access output iterator");

        using value_type = std::iter_value_t<Iterator>;

        unsigned long const length = std::distance(first, last);

        if (length == 0)
        {
            return out;
        }

        constexpr unsigned long min_per_thread = 25;
        unsigned long const max_threads = (length + min_per_thread - 1) / min_per_thread;
        unsigned long const hardware_threads = std::thread::hardware_concurrency();
        unsigned long const num_threads =
            std::min(hardware_threads != 0 ? hardware_threads : 2, max_threads);

        if (length < (2 * min_per_thread) || num_threads < 2)
        {
            return std::inclusive_scan(first, last, out, op);
        }

        unsigned long const block_size = length / num_threads;
        thread_pool& pool = default_thread_pool();

        // Phase 1: per-block totals.
        std::vector<std::future<value_type>> total_futures;
        total_futures.reserve(num_threads);
        {
            Iterator block_first = first;
            for (unsigned long i = 0; i < num_threads; ++i)
            {
                Iterator const block_last =
                    (i + 1 == num_threads) ? last : block_first + block_size;
                total_futures.push_back(pool.submit([block_first, block_last, op] {
                    value_type total = *block_first;
                    return std::accumulate(std::next(block_first), block_last,
                                           std::move(total), op);
                }));
                block_first = block_last;
            }
        }

        std::vector<value_type> totals;
        totals.reserve(num_threads);
        for (auto& future : total_futures)
        {
            while (future.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            {
                pool.run_pending_task();
            }
            totals.push_back(future.get());
        }

        // Phase 2: exclusive prefix of the block totals (sequential, tiny).
        std::vector<value_type> carries(num_threads);
        for (unsigned long i = 1; i < num_threads; ++i)
        {
            carries[i] = (i == 1) ? totals[0] : op(carries[i - 1], totals[i - 1]);
        }

        // Phase 3: per-block local scans seeded with the block carry.
        std::vector<std::future<void>> scan_futures;
        scan_futures.reserve(num_threads);
        Iterator block_first = first;
        for (unsigned long i = 0; i < num_threads; ++i)
        {
            Iterator const block_last =
                (i + 1 == num_threads) ? last : block_first + block_size;
            OutIt const block_out = out + (block_first - first);
            value_type const carry = carries[i];

            if (i == 0)
            {
                scan_futures.push_back(pool.submit([block_first, block_last, block_out, op] {
                    std::inclusive_scan(block_first, block_last, block_out, op);
                }));
            }
            else
            {
                scan_futures.push_back(
                    pool.submit([block_first, block_last, block_out, op, carry] {
                        std::inclusive_scan(block_first, block_last, block_out, op, carry);
                    }));
            }
            block_first = block_last;
        }

        for (auto& future : scan_futures)
        {
            while (future.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            {
                pool.run_pending_task();
            }
            future.get();
        }

        return out + length;
    }

    template<typename Iterator, typename OutIt>
    OutIt parallel_inclusive_scan(Iterator first, Iterator last, OutIt out)
    {
        return parallel_inclusive_scan(first, last, out, std::plus<>());
    }

    /// Parallel exclusive scan on the shared default thread pool. Same
    /// structure as parallel_inclusive_scan; init only seeds the first block,
    /// later blocks start from op(init, carry). The result matches
    /// std::exclusive_scan(first, last, out, init, op).
    template<typename Iterator, typename OutIt, typename T, typename BinaryOp>
    OutIt parallel_exclusive_scan(Iterator first, Iterator last, OutIt out, T init, BinaryOp op)
    {
        static_assert(std::random_access_iterator<Iterator>,
                      "parallel_exclusive_scan requires a random-access iterator");
        static_assert(std::random_access_iterator<OutIt>,
                      "parallel_exclusive_scan requires a random-access output iterator");

        using value_type = std::iter_value_t<Iterator>;

        unsigned long const length = std::distance(first, last);

        if (length == 0)
        {
            return out;
        }

        constexpr unsigned long min_per_thread = 25;
        unsigned long const max_threads = (length + min_per_thread - 1) / min_per_thread;
        unsigned long const hardware_threads = std::thread::hardware_concurrency();
        unsigned long const num_threads =
            std::min(hardware_threads != 0 ? hardware_threads : 2, max_threads);

        if (length < (2 * min_per_thread) || num_threads < 2)
        {
            return std::exclusive_scan(first, last, out, init, op);
        }

        unsigned long const block_size = length / num_threads;
        thread_pool& pool = default_thread_pool();

        // Phase 1: per-block totals.
        std::vector<std::future<value_type>> total_futures;
        total_futures.reserve(num_threads);
        {
            Iterator block_first = first;
            for (unsigned long i = 0; i < num_threads; ++i)
            {
                Iterator const block_last =
                    (i + 1 == num_threads) ? last : block_first + block_size;
                total_futures.push_back(pool.submit([block_first, block_last, op] {
                    value_type total = *block_first;
                    return std::accumulate(std::next(block_first), block_last,
                                           std::move(total), op);
                }));
                block_first = block_last;
            }
        }

        std::vector<value_type> totals;
        totals.reserve(num_threads);
        for (auto& future : total_futures)
        {
            while (future.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            {
                pool.run_pending_task();
            }
            totals.push_back(future.get());
        }

        // Phase 2: block seeds — seed[0] = init, seed[i] = op(seed[i-1], totals[i-1]).
        std::vector<T> seeds(num_threads);
        seeds[0] = init;
        for (unsigned long i = 1; i < num_threads; ++i)
        {
            seeds[i] = op(seeds[i - 1], totals[i - 1]);
        }

        // Phase 3: per-block local exclusive scans seeded per block.
        std::vector<std::future<void>> scan_futures;
        scan_futures.reserve(num_threads);
        Iterator block_first = first;
        for (unsigned long i = 0; i < num_threads; ++i)
        {
            Iterator const block_last =
                (i + 1 == num_threads) ? last : block_first + block_size;
            OutIt const block_out = out + (block_first - first);
            T const seed = seeds[i];

            scan_futures.push_back(
                pool.submit([block_first, block_last, block_out, seed, op] {
                    std::exclusive_scan(block_first, block_last, block_out, seed, op);
                }));
            block_first = block_last;
        }

        for (auto& future : scan_futures)
        {
            while (future.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            {
                pool.run_pending_task();
            }
            future.get();
        }

        return out + length;
    }

    template<typename Iterator, typename OutIt, typename T>
    OutIt parallel_exclusive_scan(Iterator first, Iterator last, OutIt out, T init)
    {
        return parallel_exclusive_scan(first, last, out, init, std::plus<>());
    }
}
