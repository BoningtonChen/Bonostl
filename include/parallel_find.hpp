//
// Created by 陈奕锟 on 2022/12/11.
//

#ifndef BONOSTL_PARALLEL_FIND_HPP
#define BONOSTL_PARALLEL_FIND_HPP

#endif //BONOSTL_PARALLEL_FIND_HPP
#include "bonostlpch.h"

namespace Bonostl
{
    template<typename Iterator, typename MatchType>
    Iterator parallel_find_impl(
            Iterator first, Iterator last,
            MatchType match,
            std::atomic<bool>& done
    )
    {
        try
        {
            unsigned long const length = std::distance(first, last);
            unsigned long const min_per_thread = 25;

            if ( length < (2 * min_per_thread) )
            {
                for ( ; (first != last) && !done.load(); first++ )
                {
                    if (*first == match)
                    {
                        done = true;
                        return first;
                    }
                }
                return last;
            }
            else [[likely]]
            {
                Iterator const mid_point = first + (length / 2);
                std::future<Iterator> async_result =
                        std::async(
                                &parallel_find_impl<Iterator, MatchType>,
                                mid_point, last,
                                match, std::ref(done)
                        );

                Iterator const direct_result =
                        parallel_find_impl(first, mid_point, match, done);

                return (direct_result == mid_point) ? async_result.get() : direct_result;
            }
        }
        catch (...)
        {
            done = true;
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