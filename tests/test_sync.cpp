#include <catch_amalgamated.hpp>

#include <atomic>
#include <shared_mutex>
#include <thread>
#include <vector>

#include "seqlock.hpp"
#include "shared_spinlock.hpp"

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

TEST_CASE("shared_spinlock: exclusive writers guard a counter", "[sync]")
{
    Bonostl::shared_spinlock lock;
    int counter = 0;

    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t)
    {
        threads.emplace_back([&] {
            for (int i = 0; i < 1000; ++i)
            {
                std::lock_guard guard(lock);
                ++counter;
            }
        });
    }
    for (auto& thread : threads)
    {
        thread.join();
    }

    REQUIRE(counter == 4000);
}

TEST_CASE("shared_spinlock: readers overlap, writer excluded", "[sync]")
{
    Bonostl::shared_spinlock lock;
    std::atomic<int> readers_inside{0};
    std::atomic<bool> both_inside{false};
    std::atomic<bool> release{false};

    auto reader = [&] {
        std::shared_lock guard(lock);
        readers_inside.fetch_add(1, std::memory_order_acq_rel);
        if (readers_inside.load(std::memory_order_acquire) == 2)
        {
            both_inside.store(true, std::memory_order_release);
        }
        while (!release.load(std::memory_order_acquire))
        {
            std::this_thread::yield();
        }
    };

    std::thread r1(reader);
    std::thread r2(reader);

    while (!both_inside.load(std::memory_order_acquire))
    {
        std::this_thread::yield();
    }

    // Both readers hold the shared lock: a writer cannot enter.
    REQUIRE_FALSE(lock.try_lock());

    release.store(true, std::memory_order_release);
    r1.join();
    r2.join();

    // Readers gone: the writer can take the lock.
    REQUIRE(lock.try_lock());
    lock.unlock();
}

TEST_CASE("shared_spinlock: try_lock_shared fails while writer holds", "[sync]")
{
    Bonostl::shared_spinlock lock;

    lock.lock();
    REQUIRE_FALSE(lock.try_lock_shared());
    lock.unlock();

    REQUIRE(lock.try_lock_shared());
    lock.unlock_shared();
}
