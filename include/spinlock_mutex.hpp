//
// Created by 陈奕锟 on 2022/12/3.
//

#ifndef BONOSTL_SPINLOCK_MUTEX_HPP
#define BONOSTL_SPINLOCK_MUTEX_HPP

#endif //BONOSTL_SPINLOCK_MUTEX_HPP

#include "bonostlpch.h"

namespace Bonostl {

    class spinlock_mutex {
    private:
        std::atomic_flag flag;

    public:
        spinlock_mutex()
                : flag(ATOMIC_FLAG_INIT) {}

        void lock() {
            while (flag.test_and_set(std::memory_order_acquire));
        }

        void unlock() {
            flag.clear(std::memory_order_relaxed);
        }
    };

}