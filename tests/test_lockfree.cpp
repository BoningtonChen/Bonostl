#include <catch_amalgamated.hpp>

#include <algorithm>
#include <atomic>
#include <thread>
#include <vector>

#include "lockfree_list.hpp"
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

TEST_CASE("lockfree_list: sequential insert, contains, erase keep order", "[lockfree]")
{
    Bonostl::lockfree_list<int> list;

    REQUIRE(list.empty());

    for (int v : {5, 1, 9, 3, 7})
    {
        REQUIRE(list.insert(v));
    }

    REQUIRE_FALSE(list.empty());
    REQUIRE(list.contains(1));
    REQUIRE(list.contains(9));
    REQUIRE_FALSE(list.contains(4));

    std::vector<int> seen;
    list.for_each([&](int v) { seen.push_back(v); });
    REQUIRE(seen == std::vector<int>{1, 3, 5, 7, 9});

    REQUIRE(list.erase(5));
    REQUIRE_FALSE(list.contains(5));
    REQUIRE_FALSE(list.erase(5));

    seen.clear();
    list.for_each([&](int v) { seen.push_back(v); });
    REQUIRE(seen == std::vector<int>{1, 3, 7, 9});
}

TEST_CASE("lockfree_list: concurrent disjoint inserts", "[lockfree]")
{
    Bonostl::lockfree_list<int> list;

    constexpr int per_thread = 500;
    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t)
    {
        threads.emplace_back([&, t] {
            for (int i = 0; i < per_thread; ++i)
            {
                list.insert(t * per_thread + i);
            }
        });
    }
    for (auto& thread : threads)
    {
        thread.join();
    }

    for (int v = 0; v < 4 * per_thread; ++v)
    {
        REQUIRE(list.contains(v));
    }

    std::vector<int> seen;
    list.for_each([&](int v) { seen.push_back(v); });
    REQUIRE(seen.size() == 4 * per_thread);
    REQUIRE(std::is_sorted(seen.begin(), seen.end()));
}

TEST_CASE("lockfree_list: concurrent erase", "[lockfree]")
{
    Bonostl::lockfree_list<int> list;

    constexpr int total = 2000;
    for (int v = 0; v < total; ++v)
    {
        list.insert(v);
    }

    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t)
    {
        threads.emplace_back([&, t] {
            int const lo = t * (total / 4);
            int const hi = lo + total / 4;
            for (int v = lo; v < hi; ++v)
            {
                if (v % 2 == 0)
                {
                    list.erase(v);
                }
            }
        });
    }
    for (auto& thread : threads)
    {
        thread.join();
    }

    for (int v = 0; v < total; ++v)
    {
        REQUIRE(list.contains(v) == ((v % 2) != 0));
    }
}

TEST_CASE("lockfree_list: mixed insert/erase stress", "[lockfree]")
{
    Bonostl::lockfree_list<int> list;

    constexpr int per_thread = 500;
    std::atomic<int> inserted{0};
    std::atomic<int> erased{0};

    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t)
    {
        threads.emplace_back([&, t] {
            for (int i = 0; i < per_thread; ++i)
            {
                if (list.insert(t * per_thread + i))
                {
                    ++inserted;
                }
            }
            for (int i = 0; i < per_thread; i += 2)
            {
                if (list.erase(t * per_thread + i))
                {
                    ++erased;
                }
            }
        });
    }
    for (auto& thread : threads)
    {
        thread.join();
    }

    REQUIRE(inserted.load() == 4 * per_thread);
    REQUIRE(erased.load() == 2 * per_thread);

    for (int t = 0; t < 4; ++t)
    {
        for (int i = 0; i < per_thread; ++i)
        {
            bool const should_exist = (i % 2) != 0;
            REQUIRE(list.contains(t * per_thread + i) == should_exist);
        }
    }
}

TEST_CASE("lockfree_list: concurrent insert and erase of the same keys", "[lockfree]")
{
    Bonostl::lockfree_list<int> list;

    constexpr int key_range = 500;

    std::thread inserter([&] {
        for (int round = 0; round < 4; ++round)
        {
            for (int v = 0; v < key_range; ++v)
            {
                list.insert(v);
            }
        }
    });

    std::thread eraser([&] {
        for (int round = 0; round < 4; ++round)
        {
            for (int v = 0; v < key_range; ++v)
            {
                list.erase(v);
            }
        }
    });

    inserter.join();
    eraser.join();

    // Any final state is legal; the list must stay intact, sorted and
    // duplicate-free.
    std::vector<int> seen;
    list.for_each([&](int v) { seen.push_back(v); });
    REQUIRE(std::is_sorted(seen.begin(), seen.end()));
    REQUIRE(std::adjacent_find(seen.begin(), seen.end()) == seen.end());
    REQUIRE(seen.size() <= key_range);
}
