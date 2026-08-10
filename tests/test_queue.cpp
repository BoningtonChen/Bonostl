#include <catch_amalgamated.hpp>

#include <atomic>
#include <string>
#include <thread>
#include <vector>

#include "queue.hpp"
#include "threadsafe_list.hpp"
#include "threadsafe_lookup_table.hpp"
#include "threadsafe_queue.hpp"
#include "threadsafe_stack.hpp"

TEST_CASE("queue: FIFO order and empty state", "[queue]")
{
    Bonostl::queue<int> q;

    REQUIRE_FALSE(q.try_pop().has_value());
    q.push(1);
    q.push(2);
    q.push(3);

    REQUIRE(*q.try_pop() == 1);
    REQUIRE(*q.try_pop() == 2);
    REQUIRE(*q.try_pop() == 3);
    REQUIRE_FALSE(q.try_pop().has_value());
}

TEST_CASE("queue: move-only element type", "[queue]")
{
    Bonostl::queue<std::unique_ptr<int>> q;
    q.push(std::make_unique<int>(7));

    auto value = q.try_pop();
    REQUIRE(value.has_value());
    REQUIRE(**value == 7);
    REQUIRE_FALSE(q.try_pop().has_value());
}

TEST_CASE("threadsafe_stack: LIFO order", "[stack]")
{
    Bonostl::threadsafe_stack<int> s;

    REQUIRE_FALSE(s.pop().has_value());
    s.push(1);
    s.push(2);
    s.push(3);

    REQUIRE(*s.pop() == 3);
    REQUIRE(*s.pop() == 2);

    int value = 0;
    REQUIRE(s.try_pop(value));
    REQUIRE(value == 1);
    REQUIRE_FALSE(s.try_pop(value));
    REQUIRE(s.empty());
}

TEST_CASE("threadsafe_stack: concurrent push/pop count", "[stack]")
{
    Bonostl::threadsafe_stack<int> s;
    constexpr int per_thread = 5000;

    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t)
    {
        threads.emplace_back([&] {
            for (int i = 0; i < per_thread; ++i)
            {
                s.push(t * per_thread + i);
            }
        });
    }

    std::atomic<int> popped{0};
    for (int t = 0; t < 4; ++t)
    {
        threads.emplace_back([&] {
            while (popped.load() < 4 * per_thread)
            {
                if (s.pop())
                {
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
    REQUIRE(s.empty());
}

TEST_CASE("threadsafe_queue: FIFO and blocking wait", "[queue]")
{
    Bonostl::threadsafe_queue<int> q;

    REQUIRE_FALSE(q.try_pop().has_value());
    q.push(1);
    q.push(2);

    REQUIRE(q.wait_and_pop() == 1);

    int value = 0;
    REQUIRE(q.try_pop(value));
    REQUIRE(value == 2);
    REQUIRE_FALSE(q.try_pop(value));
    REQUIRE(q.empty());
}

TEST_CASE("threadsafe_queue: wait_and_pop blocks until data arrives", "[queue]")
{
    Bonostl::threadsafe_queue<int> q;

    std::thread producer([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        q.push(42);
    });

    REQUIRE(q.wait_and_pop() == 42);
    producer.join();
}

TEST_CASE("threadsafe_queue: concurrent producer/consumer", "[queue]")
{
    Bonostl::threadsafe_queue<int> q;
    constexpr int per_producer = 2000;

    std::atomic<bool> stop{false};
    std::atomic<int> popped{0};

    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t)
    {
        threads.emplace_back([&, t] {
            for (int i = 0; i < per_producer; ++i)
            {
                q.push(t * per_producer + i);
            }
        });
    }

    std::atomic<int> observed{0};
    for (int t = 0; t < 4; ++t)
    {
        threads.emplace_back([&] {
            while (!stop.load())
            {
                if (auto value = q.try_pop())
                {
                    observed.fetch_add(*value);
                    ++popped;
                }
                else
                {
                    std::this_thread::yield();
                }
            }
        });
    }

    for (int t = 0; t < 4; ++t)
    {
        threads[t].join();
    }

    while (popped.load() < 4 * per_producer)
    {
        std::this_thread::yield();
    }
    stop = true;

    for (int t = 4; t < 8; ++t)
    {
        threads[t].join();
    }

    REQUIRE(popped == 4 * per_producer);

    int expected_sum = 0;
    for (int t = 0; t < 4; ++t)
    {
        for (int i = 0; i < per_producer; ++i)
        {
            expected_sum += t * per_producer + i;
        }
    }
    REQUIRE(observed == expected_sum);
    REQUIRE(q.empty());
}

TEST_CASE("threadsafe_lookup_table: CRUD and snapshot", "[lookup]")
{
    Bonostl::threadsafe_lookup_table<int, std::string> table;

    REQUIRE_FALSE(table.value_for(1).has_value());

    table.add_or_update_mapping(1, "one");
    table.add_or_update_mapping(2, "two");
    table.add_or_update_mapping(3, "three");
    table.add_or_update_mapping(1, "ONE");

    REQUIRE(*table.value_for(1) == "ONE");
    REQUIRE(*table.value_for(2) == "two");
    REQUIRE(*table.value_for(3) == "three");

    auto const map = table.get_map();
    REQUIRE(map.size() == 3);
    REQUIRE(map.at(1) == "ONE");

    table.remove_mapping(2);
    REQUIRE_FALSE(table.value_for(2).has_value());
    REQUIRE(table.get_map().size() == 2);
}

TEST_CASE("threadsafe_lookup_table: concurrent add/remove/read", "[lookup]")
{
    Bonostl::threadsafe_lookup_table<int, int> table;
    constexpr int key_count = 50;

    std::vector<std::thread> writers;
    for (int t = 0; t < 4; ++t)
    {
        writers.emplace_back([&, t] {
            for (int i = 0; i < key_count; ++i)
            {
                table.add_or_update_mapping(i, t);
            }
        });
    }

    std::atomic<int> reads{0};
    std::vector<std::thread> readers;
    for (int t = 0; t < 4; ++t)
    {
        readers.emplace_back([&] {
            for (int i = 0; i < key_count; ++i)
            {
                if (table.value_for(i).has_value())
                {
                    ++reads;
                }
            }
        });
    }

    for (auto& thread : writers)
    {
        thread.join();
    }
    for (auto& thread : readers)
    {
        thread.join();
    }

    REQUIRE(table.get_map().size() == key_count);
    REQUIRE(reads > 0);
}

TEST_CASE("threadsafe_list: push/for_each/find/remove", "[list]")
{
    Bonostl::threadsafe_list<std::string> list;

    list.push_front("one");
    list.push_front("two");
    list.push_front("three");

    std::size_t count = 0;
    list.for_each([&](std::string const&) { ++count; });
    REQUIRE(count == 3);

    auto const found = list.find_first_if([](std::string const& s) { return s == "two"; });
    REQUIRE(found != nullptr);
    REQUIRE(*found == "two");

    REQUIRE_FALSE(list.find_first_if([](std::string const& s) { return s == "nope"; }));

    list.remove_if([](std::string const& s) { return s == "two"; });

    count = 0;
    list.for_each([&](std::string const&) { ++count; });
    REQUIRE(count == 2);
}

TEST_CASE("threadsafe_list: concurrent push_front", "[list]")
{
    Bonostl::threadsafe_list<int> list;

    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t)
    {
        threads.emplace_back([&] {
            for (int i = 0; i < 1000; ++i)
            {
                list.push_front(i);
            }
        });
    }

    for (auto& thread : threads)
    {
        thread.join();
    }

    std::size_t count = 0;
    list.for_each([&](int) { ++count; });
    REQUIRE(count == 4000);
}
