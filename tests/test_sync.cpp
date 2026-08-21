#include <catch_amalgamated.hpp>

#include <atomic>
#include <thread>
#include <vector>

#include "seqlock.hpp"

namespace
{
    struct shared_state
    {
        int a = 0;
        int b = 0;
    };
}

TEST_CASE("seqlock: single writer, single reader", "[sync]")
{
    Bonostl::seqlock lock;
    shared_state state;

    constexpr int iterations = 10000;

    std::thread writer([&] {
        for (int i = 1; i <= iterations; ++i)
        {
            Bonostl::seqlock::write_guard guard(lock);
            state.a = i;
            state.b = i;
        }
    });

    std::thread reader([&] {
        int last_seen = 0;
        for (;;)
        {
            unsigned const seq = lock.read_begin();
            shared_state const snapshot = state;
            if (lock.read_retry(seq))
            {
                // A consistent snapshot always has a == b.
                REQUIRE(snapshot.a == snapshot.b);
                REQUIRE(snapshot.a >= last_seen);
                last_seen = snapshot.a;
                if (last_seen == iterations)
                {
                    break;
                }
            }
        }
    });

    writer.join();
    reader.join();
}

TEST_CASE("seqlock: multiple writers serialize", "[sync]")
{
    Bonostl::seqlock lock;
    shared_state state;

    constexpr int per_writer = 2000;
    std::atomic<int> torn_reads{0};

    std::vector<std::thread> writers;
    for (int t = 0; t < 2; ++t)
    {
        writers.emplace_back([&] {
            for (int i = 0; i < per_writer; ++i)
            {
                Bonostl::seqlock::write_guard guard(lock);
                ++state.a;
                ++state.b;
            }
        });
    }

    std::vector<std::thread> readers;
    for (int t = 0; t < 4; ++t)
    {
        readers.emplace_back([&] {
            for (int i = 0; i < per_writer * 2; ++i)
            {
                unsigned const seq = lock.read_begin();
                shared_state const snapshot = state;
                if (lock.read_retry(seq) && snapshot.a != snapshot.b)
                {
                    torn_reads.fetch_add(1, std::memory_order_relaxed);
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

    REQUIRE(state.a == 2 * per_writer);
    REQUIRE(state.b == 2 * per_writer);
    REQUIRE(torn_reads.load() == 0);
}
