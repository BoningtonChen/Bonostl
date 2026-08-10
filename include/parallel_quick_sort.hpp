#pragma once

#include "bonostlpch.h"

#include "threadsafe_stack.hpp"

namespace Bonostl
{
    template<typename T>
    struct sorter
    {
        struct chunk_to_sort
        {
            std::list<T> data;
            std::promise<std::list<T>> promise;
        };

        sorter()
            : owner_id(std::this_thread::get_id()),
              max_thread_count(compute_max_thread_count()),
              end_of_data(false)
        {
        }

        sorter(const sorter&) = delete;
        sorter& operator=(const sorter&) = delete;

        ~sorter()
        {
            end_of_data = true;

            for (auto& thread : threads)
            {
                thread.join();
            }
        }

        std::list<T> do_sort(std::list<T> chunk_data)
        {
            if (chunk_data.empty())
            {
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

            chunk_to_sort new_lower_chunk;
            new_lower_chunk.data.splice(
                new_lower_chunk.data.end(), chunk_data, chunk_data.begin(), divide_point);

            if (chunk_data.empty())
            {
                new_lower_chunk.data.sort();
                result.splice(result.begin(), new_lower_chunk.data);
                return result;
            }

            std::future<std::list<T>> new_lower = new_lower_chunk.promise.get_future();
            chunks.push(std::move(new_lower_chunk));

            if (std::this_thread::get_id() == owner_id && threads.size() < max_thread_count)
            {
                threads.emplace_back(&sorter::sort_thread, this);
            }

            std::list<T> new_higher(do_sort(std::move(chunk_data)));
            result.splice(result.end(), new_higher);

            while (new_lower.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            {
                try_sort_chunk();
            }

            result.splice(result.begin(), new_lower.get());
            return result;
        }

    private:
        static unsigned compute_max_thread_count()
        {
            unsigned const hardware_threads = std::thread::hardware_concurrency();
            return hardware_threads > 1 ? hardware_threads - 1 : 1;
        }

        void try_sort_chunk()
        {
            if (auto chunk = chunks.pop())
            {
                sort_chunk(std::move(*chunk));
            }
        }

        void sort_chunk(chunk_to_sort chunk)
        {
            chunk.promise.set_value(do_sort(std::move(chunk.data)));
        }

        void sort_thread()
        {
            while (!end_of_data)
            {
                try_sort_chunk();
                std::this_thread::yield();
            }
        }

        threadsafe_stack<chunk_to_sort> chunks;
        std::vector<std::thread> threads;
        std::thread::id const owner_id;
        unsigned const max_thread_count;
        std::atomic<bool> end_of_data;
    };

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
