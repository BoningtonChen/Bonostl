#include <catch_amalgamated.hpp>

#include <atomic>
#include <thread>
#include <vector>

#include "lockfree_queue.hpp"
#include "lockfree_stack.hpp"

TEST_CASE("lockfree_stack: LIFO order and empty state", "[lockfree]")
{
    Bonostl::lockfree_stack<int> s;

    REQUIRE_FALSE(s.pop().has_value());
    s.push(1);
    s.push(2);
    s.push(3);

    REQUIRE(*s.pop() == 3);
    REQUIRE(*s.pop() == 2);
    REQUIRE(*s.pop() == 1);
    REQUIRE_FALSE(s.pop().has_value());
}

TEST_CASE("lockfree_queue: FIFO order and empty state", "[lockfree]")
{
    Bonostl::lockfree_queue<int> q;

    REQUIRE_FALSE(q.pop().has_value());
    q.push(1);
    q.push(2);
    q.push(3);

    REQUIRE(*q.pop() == 1);
    REQUIRE(*q.pop() == 2);
    REQUIRE(*q.pop() == 3);
    REQUIRE_FALSE(q.pop().has_value());
}

TEST_CASE("lockfree_stack: concurrent push/pop keep element set intact", "[lockfree]")
{
    Bonostl::lockfree_stack<int> s;
    constexpr int per_thread = 5000;

    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t)
    {
        threads.emplace_back([&, t] {
            for (int i = 0; i < per_thread; ++i)
            {
                s.push(t * per_thread + i);
            }
        });
    }
    for (auto& thread : threads)
    {
        thread.join();
    }

    std::atomic<int> popped{0};
    std::atomic<long long> observed_sum{0};
    threads.clear();
    for (int t = 0; t < 4; ++t)
    {
        threads.emplace_back([&] {
            while (popped.load() < 4 * per_thread)
            {
                if (auto value = s.pop())
                {
                    observed_sum.fetch_add(*value);
                    ++popped;
                }
                else
                {
                    std::this_thread::yield();
                }
            }
        });
    }
    for (auto& thread : threads)
    {
        thread.join();
    }

    REQUIRE(popped == 4 * per_thread);

    long long expected_sum = 0;
    for (int t = 0; t < 4; ++t)
    {
        for (int i = 0; i < per_thread; ++i)
        {
            expected_sum += t * per_thread + i;
        }
    }
    REQUIRE(observed_sum == expected_sum);
    REQUIRE_FALSE(s.pop().has_value());
}

TEST_CASE("lockfree_queue: concurrent push/pop keep element set intact", "[lockfree]")
{
    Bonostl::lockfree_queue<int> q;
    constexpr int per_thread = 5000;

    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t)
    {
        threads.emplace_back([&, t] {
            for (int i = 0; i < per_thread; ++i)
            {
                q.push(t * per_thread + i);
            }
        });
    }
    for (auto& thread : threads)
    {
        thread.join();
    }

    std::atomic<int> popped{0};
    std::atomic<long long> observed_sum{0};
    threads.clear();
    for (int t = 0; t < 4; ++t)
    {
        threads.emplace_back([&] {
            while (popped.load() < 4 * per_thread)
            {
                if (auto value = q.pop())
                {
                    observed_sum.fetch_add(*value);
                    ++popped;
                }
                else
                {
                    std::this_thread::yield();
                }
            }
        });
    }
    for (auto& thread : threads)
    {
        thread.join();
    }

    REQUIRE(popped == 4 * per_thread);

    long long expected_sum = 0;
    for (int t = 0; t < 4; ++t)
    {
        for (int i = 0; i < per_thread; ++i)
        {
            expected_sum += t * per_thread + i;
        }
    }
    REQUIRE(observed_sum == expected_sum);
    REQUIRE_FALSE(q.pop().has_value());
}
