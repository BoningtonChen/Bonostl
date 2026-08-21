#pragma once

#include "bonostlpch.h"
#include "join_threads.hpp"

namespace Bonostl
{
    /// Accumulates the block [first, last) with op into result, starting from
    /// the first element so that non-commutative initial values (e.g.
    /// multiplication) behave correctly. The element type is
    /// std::iter_value_t<Iterator>.
    template<typename Iterator, typename T, typename BinaryOp>
    struct accumulate_block
    {
        void operator()(Iterator first, Iterator last, T& result, BinaryOp op)
        {
            std::iter_value_t<Iterator> const first_value = *first;
            result = std::accumulate(std::next(first), last, first_value, op);
        }
    };

    /// Parallel accumulate (C++ Concurrency in Action, listing 8.4).
    /// The range is split into blocks reduced concurrently with op; the block
    /// results are then merged with op. op must be associative, since the
    /// parallel reordering changes the order of application.
    template<typename Iterator, typename T, typename BinaryOp>
    T parallel_accumulate(Iterator first, Iterator last, T init, BinaryOp op)
    {
        unsigned long const length = std::distance(first, last);

        if (length == 0)
        {
            return init;
        }

        unsigned long const min_per_thread = 25;

        if (length < (2 * min_per_thread))
        {
            return std::accumulate(first, last, init, op);
        }
        else [[likely]]
        {
            unsigned long const max_threads = (length + min_per_thread - 1) / min_per_thread;
            unsigned long const hardware_threads = std::thread::hardware_concurrency();
            unsigned long const num_threads = std::min(
                hardware_threads != 0 ? hardware_threads : 2, max_threads);
            unsigned long const block_size = length / num_threads;

            std::vector<T> results(num_threads);
            std::vector<std::thread> threads(num_threads - 1);

            join_threads joiner(threads);

            Iterator block_start = first;
            for (unsigned long i = 0; i < (num_threads - 1); ++i)
            {
                Iterator block_end = block_start;
                std::advance(block_end, block_size);
                threads[i] = std::thread(
                    accumulate_block<Iterator, T, BinaryOp>(), block_start, block_end,
                    std::ref(results[i]), op);
                block_start = block_end;
            }

            accumulate_block<Iterator, T, BinaryOp>()(
                block_start, last, results[num_threads - 1], op);

            for (auto& thread : threads)
            {
                thread.join();
            }

            return std::accumulate(results.begin(), results.end(), init, op);
        }
    }

    template<typename Iterator, typename T>
    T parallel_accumulate(Iterator first, Iterator last, T init)
    {
        return parallel_accumulate(first, last, init, std::plus<>());
    }
}