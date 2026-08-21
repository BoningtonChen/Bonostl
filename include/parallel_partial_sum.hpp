#pragma once

#include "bonostlpch.h"
#include "join_threads.hpp"

namespace Bonostl
{
    template<typename Iterator>
    void parallel_partial_sum(Iterator first, Iterator last)
    {
        using value_type = std::iter_value_t<Iterator>;

        struct process_chunk
        {
            void operator()(Iterator begin, Iterator chunk_last,
                            std::future<value_type>* previous_end_value,
                            std::promise<value_type>* end_value)
            {
                try
                {
                    Iterator end = chunk_last;
                    ++end;

                    std::partial_sum(begin, end, begin);

                    if (previous_end_value)
                    {
                        value_type addend = previous_end_value->get();
                        *chunk_last += addend;

                        if (end_value)
                        {
                            end_value->set_value(*chunk_last);
                        }

                        std::ranges::for_each(begin, chunk_last, [addend](value_type& item) {
                            item += addend;
                        });
                    }
                    else if (end_value)
                    {
                        end_value->set_value(*chunk_last);
                    }
                }
                catch (...)
                {
                    if (end_value)
                    {
                        end_value->set_exception(std::current_exception());
                    }
                    else
                    {
                        throw;
                    }
                }
            }
        };

        unsigned long const length = std::distance(first, last);

        if (length == 0)
        {
            return;
        }

        constexpr unsigned long min_per_thread = 25;
        unsigned long const max_threads = (length + min_per_thread - 1) / min_per_thread;
        unsigned long const hardware_threads = std::thread::hardware_concurrency();
        unsigned long const num_threads = std::min(hardware_threads != 0 ? hardware_threads : 2, max_threads);

        unsigned long const block_size = length / num_threads;

        std::vector<std::thread> threads(num_threads - 1);
        std::vector<std::promise<value_type>> end_values(num_threads - 1);
        std::vector<std::future<value_type>> previous_end_values;
        previous_end_values.reserve(num_threads - 1);

        join_threads joiner(threads);

        Iterator block_start = first;

        for (unsigned long i = 0; i < num_threads - 1; ++i)
        {
            Iterator block_last = block_start;
            std::advance(block_last, block_size - 1);

            threads[i] = std::thread(process_chunk{},
                                     block_start, block_last,
                                     (i != 0) ? &previous_end_values[i - 1] : nullptr,
                                     &end_values[i]);

            block_start = block_last;
            ++block_start;

            previous_end_values.push_back(end_values[i].get_future());
        }

        Iterator final_element = block_start;
        std::advance(final_element, std::distance(block_start, last) - 1);

        process_chunk{}(block_start, final_element,
                        num_threads > 1 ? &previous_end_values.back() : nullptr,
                        nullptr);
    }
}
