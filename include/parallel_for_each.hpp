#pragma once

#include "bonostlpch.h"

namespace Bonostl
{
    template<typename Iterator, typename Func>
    void parallel_for_each(Iterator first, Iterator last, Func func)
    {
        unsigned long const length = std::distance(first, last);

        if (length == 0)
        {
            return;
        }

        constexpr unsigned long min_per_thread = 25;

        if (length < (2 * min_per_thread))
        {
            std::ranges::for_each(first, last, func);
        }
        else [[likely]]
        {
            Iterator const mid_point = first + length / 2;
            std::future<void> first_half = std::async([&] {
                parallel_for_each(first, mid_point, func);
            });

            parallel_for_each(mid_point, last, func);
            first_half.get();
        }
    }
}
