#pragma once

#include "bonostlpch.h"

namespace Bonostl
{
    class join_threads
    {
    public:
        explicit join_threads(std::vector<std::thread>& threads) noexcept
            : threads_(threads)
        {
        }

        join_threads(const join_threads&) = delete;
        join_threads& operator=(const join_threads&) = delete;

        ~join_threads()
        {
            for (auto& thread : threads_)
            {
                if (thread.joinable())
                {
                    thread.join();
                }
            }
        }

    private:
        std::vector<std::thread>& threads_;
    };
}
