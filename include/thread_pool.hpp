#pragma once

#include "bonostlpch.h"
#include "function_wrapper.hpp"
#include "join_threads.hpp"
#include "threadsafe_queue.hpp"
#include "work_stealing_queue.hpp"

namespace Bonostl
{
    /// Work-stealing thread pool (C++ Concurrency in Action, listing 9.6-9.8).
    /// Tasks are stored in a shared threadsafe_queue plus one local
    /// work_stealing_queue per worker; workers prefer their own queue, then the
    /// shared queue, then steal from other workers' queues.
    ///
    /// Usage constraints:
    /// - A pool task must not block waiting on the future of another task
    ///   submitted to the same pool; on a single-thread pool this always
    ///   deadlocks. (Wait-while-helping arrives with the algorithm layer.)
    /// - A thread is affiliated with at most one pool: submitting to pool B
    ///   from a worker of pool A enqueues the task into A's local queue.
    /// - The destructor drops queued-but-unstarted tasks; their futures throw
    ///   std::future_error (broken_promise) instead of hanging.
    /// - Destruction must not race with any member call.
    class thread_pool
    {
    public:
        thread_pool()
            : thread_pool(std::thread::hardware_concurrency()
                              ? std::thread::hardware_concurrency()
                              : 2)
        {
        }

        explicit thread_pool(unsigned thread_count)
            : joiner_(threads_)
        {
            unsigned const count = std::max(thread_count, 1u);

            try
            {
                for (unsigned i = 0; i < count; ++i)
                {
                    queues_.push_back(std::make_unique<work_stealing_queue>());
                }

                for (unsigned i = 0; i < count; ++i)
                {
                    threads_.emplace_back(&thread_pool::worker_thread, this, i);
                }
            }
            catch (...)
            {
                // Keep already-started workers observable so joiner_ can join
                // them during stack unwinding instead of spinning forever.
                done_.store(true);
                throw;
            }
        }

        thread_pool(const thread_pool&) = delete;
        thread_pool& operator=(const thread_pool&) = delete;

        ~thread_pool()
        {
            done_.store(true);
        }

        template<typename F>
        std::future<std::invoke_result_t<F>> submit(F&& f)
        {
            using result_type = std::invoke_result_t<F>;
            std::packaged_task<result_type()> task(std::forward<F>(f));
            std::future<result_type> result(task.get_future());

            if (local_work_queue != nullptr)
            {
                local_work_queue->push(function_wrapper(std::move(task)));
            }
            else
            {
                pool_work_queue_.push(function_wrapper(std::move(task)));
            }

            return result;
        }

        /// Runs one pending task if available (local queue, then shared queue,
        /// then stolen). Algorithms waiting on a pool future should call this
        /// in their wait loop so blocked workers still make progress
        /// (wait-while-helping).
        void run_pending_task()
        {
            function_wrapper task;

            if (local_work_queue != nullptr && local_work_queue->try_pop(task))
            {
                task();
            }
            else if (pool_work_queue_.try_pop(task))
            {
                task();
            }
            else if (pop_task_from_other_thread_queue(task))
            {
                task();
            }
            else
            {
                std::this_thread::yield();
            }
        }

    private:
        void worker_thread(unsigned index)
        {
            my_index = index;
            local_work_queue = queues_[index].get();

            while (!done_.load())
            {
                run_pending_task();
            }
        }

        bool pop_task_from_other_thread_queue(function_wrapper& task)
        {
            unsigned const count = static_cast<unsigned>(queues_.size());
            unsigned const start = (my_index + 1 + random_steal_offset(count)) % count;

            for (unsigned i = 0; i + 1 < count; ++i)
            {
                if (queues_[(start + i) % count]->try_steal(task))
                {
                    return true;
                }
            }

            return false;
        }

        static unsigned random_steal_offset(unsigned count)
        {
            thread_local static unsigned state = 0x9e3779b9u;
            state = state * 1664525u + 1013904223u;
            return state % count;
        }

        std::atomic<bool> done_{false};
        threadsafe_queue<function_wrapper> pool_work_queue_;
        std::vector<std::unique_ptr<work_stealing_queue>> queues_;
        std::vector<std::thread> threads_;
        join_threads joiner_;

        static inline thread_local work_stealing_queue* local_work_queue = nullptr;
        static inline thread_local unsigned my_index = 0;
    };

    /// Process-wide default pool shared by the pool-based parallel algorithms.
    /// Lazily constructed on first use; lives until program exit.
    inline thread_pool& default_thread_pool()
    {
        static thread_pool pool;
        return pool;
    }
}