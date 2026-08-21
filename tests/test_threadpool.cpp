#include <catch_amalgamated.hpp>

#include <atomic>
#include <functional>
#include <future>
#include <memory>
#include <thread>
#include <vector>

#include "function_wrapper.hpp"
#include "thread_pool.hpp"
#include "work_stealing_queue.hpp"

TEST_CASE("function_wrapper: invokes wrapped callable", "[threadpool]")
{
    bool called = false;
    Bonostl::function_wrapper task([&] { called = true; });

    task();

    REQUIRE(called);
}

TEST_CASE("function_wrapper: wraps move-only callable", "[threadpool]")
{
    auto resource = std::make_unique<int>(42);
    int observed = 0;

    Bonostl::function_wrapper task([res = std::move(resource), &observed] { observed = *res; });
    task();

    REQUIRE(observed == 42);
}

TEST_CASE("function_wrapper: move construction transfers ownership", "[threadpool]")
{
    int count = 0;
    Bonostl::function_wrapper first([&] { ++count; });

    Bonostl::function_wrapper second(std::move(first));
    second();

    REQUIRE(count == 1);
}

TEST_CASE("function_wrapper: default construction then move assignment", "[threadpool]")
{
    int count = 0;
    Bonostl::function_wrapper task;

    task = Bonostl::function_wrapper([&] { count += 5; });
    task();

    REQUIRE(count == 5);
}

TEST_CASE("function_wrapper: calling empty wrapper throws", "[threadpool]")
{
    Bonostl::function_wrapper task;

    REQUIRE_THROWS_AS(task(), std::bad_function_call);
}

TEST_CASE("work_stealing_queue: try_pop yields LIFO order", "[threadpool]")
{
    Bonostl::work_stealing_queue q;
    std::vector<int> order;

    q.push(Bonostl::function_wrapper([&] { order.push_back(1); }));
    q.push(Bonostl::function_wrapper([&] { order.push_back(2); }));
    q.push(Bonostl::function_wrapper([&] { order.push_back(3); }));

    Bonostl::function_wrapper task;
    REQUIRE(q.try_pop(task));
    task();
    REQUIRE(q.try_pop(task));
    task();
    REQUIRE(q.try_pop(task));
    task();

    REQUIRE(order == std::vector<int>({3, 2, 1}));
}

TEST_CASE("work_stealing_queue: try_steal yields FIFO order", "[threadpool]")
{
    Bonostl::work_stealing_queue q;
    std::vector<int> order;

    q.push(Bonostl::function_wrapper([&] { order.push_back(1); }));
    q.push(Bonostl::function_wrapper([&] { order.push_back(2); }));
    q.push(Bonostl::function_wrapper([&] { order.push_back(3); }));

    Bonostl::function_wrapper task;
    REQUIRE(q.try_steal(task));
    task();
    REQUIRE(q.try_steal(task));
    task();
    REQUIRE(q.try_steal(task));
    task();

    REQUIRE(order == std::vector<int>({1, 2, 3}));
}

TEST_CASE("work_stealing_queue: empty queue try_pop/try_steal return false", "[threadpool]")
{
    Bonostl::work_stealing_queue q;
    Bonostl::function_wrapper task;

    REQUIRE_FALSE(q.try_pop(task));
    REQUIRE_FALSE(q.try_steal(task));
}

TEST_CASE("work_stealing_queue: concurrent push/pop/steal executes each task once", "[threadpool]")
{
    Bonostl::work_stealing_queue q;
    constexpr int total = 2000;
    std::atomic<int> executed{0};

    for (int i = 0; i < total; ++i)
    {
        q.push(Bonostl::function_wrapper([&] { ++executed; }));
    }

    std::atomic<bool> done{false};
    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t)
    {
        threads.emplace_back([&] {
            Bonostl::function_wrapper task;
            while (!done.load())
            {
                if (q.try_pop(task) || q.try_steal(task))
                {
                    task();
                }
                else
                {
                    std::this_thread::yield();
                }
            }
        });
    }

    while (executed.load() < total)
    {
        std::this_thread::yield();
    }
    done.store(true);

    for (auto& thread : threads)
    {
        thread.join();
    }

    REQUIRE(executed == total);
}

TEST_CASE("thread_pool: submit returns future with correct result", "[threadpool]")
{
    Bonostl::thread_pool pool;

    auto future = pool.submit([] { return 42; });

    REQUIRE(future.get() == 42);
}

TEST_CASE("thread_pool: concurrent submits all execute", "[threadpool]")
{
    Bonostl::thread_pool pool;
    constexpr int total = 1000;
    std::atomic<int> executed{0};

    std::vector<std::future<void>> futures;
    for (int i = 0; i < total; ++i)
    {
        futures.push_back(pool.submit([&] { ++executed; }));
    }
    for (auto& future : futures)
    {
        future.get();
    }

    REQUIRE(executed == total);
}

TEST_CASE("thread_pool: nested submit from worker executes without deadlock", "[threadpool]")
{
    Bonostl::thread_pool pool(4);
    std::atomic<int> inner{0};

    auto outer = pool.submit([&] {
        auto inner_future = pool.submit([&] { ++inner; });
        inner_future.get();
    });
    outer.get();

    REQUIRE(inner == 1);
}

TEST_CASE("thread_pool: destructor joins all threads", "[threadpool]")
{
    std::atomic<int> executed{0};
    {
        Bonostl::thread_pool pool;
        auto future = pool.submit([&] { ++executed; });
        future.get();
    }

    REQUIRE(executed == 1);
}

TEST_CASE("thread_pool: empty pool destructs cleanly", "[threadpool]")
{
    Bonostl::thread_pool pool;
}
