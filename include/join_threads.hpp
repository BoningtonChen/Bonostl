//
// Created by 陈奕锟 on 2022/12/13.
//

#ifndef BONOSTL_JOIN_THREADS_HPP
#define BONOSTL_JOIN_THREADS_HPP

#endif //BONOSTL_JOIN_THREADS_HPP
#include "bonostlpch.h"

namespace Bonostl
{
    class join_threads
    {
    private:
        std::vector<std::thread>& threads;

    public:
        explicit join_threads( std::vector<std::thread>& i_threads )
                : threads(i_threads)
        {}
        ~join_threads()
        {
            for (auto& thread : threads)
            {
                if ( thread.joinable() )
                    thread.join();
            }
        }
    };
}