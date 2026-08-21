#include <catch_amalgamated.hpp>

#include <atomic>
#include <shared_mutex>
#include <thread>
#include <vector>

#include "barrier.hpp"
#include "latch.hpp"
#include "semaphore.hpp"
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

TEST_CASE("counting_semaphore: limits concurrency", "[sync]")
{
    Bonostl::counting_semaphore semaphore(2);
    std::atomic<int> inside{0};
    std::atomic<int> max_inside{0};

    std::vector<std::thread> threads;
    for (int t = 0; t < 8; ++t)
    {
        threads.emplace_back([&] {
            semaphore.acquire();

            int const now = inside.fetch_add(1, std::memory_order_acq_rel) + 1;
            int expected = max_inside.load(std::memory_order_relaxed);
            while (now > expected
                   && !max_inside.compare_exchange_weak(expected, now,
                                                        std::memory_order_relaxed))
            {
            }

            std::this_thread::yield();
            inside.fetch_sub(1, std::memory_order_acq_rel);
            semaphore.release();
        });
    }
    for (auto& thread : threads)
    {
        thread.join();
    }

    REQUIRE(max_inside.load() <= 2);
    REQUIRE(max_inside.load() >= 1);
}

TEST_CASE("counting_semaphore: try_acquire fails when exhausted", "[sync]")
{
    Bonostl::counting_semaphore semaphore(1);

    REQUIRE(semaphore.try_acquire());
    REQUIRE_FALSE(semaphore.try_acquire());

    semaphore.release();
    REQUIRE(semaphore.try_acquire());
}

TEST_CASE("binary_semaphore: handoff between threads", "[sync]")
{
    Bonostl::binary_semaphore semaphore;
    std::atomic<bool> produced{false};

    std::thread producer([&] {
        produced.store(true, std::memory_order_release);
        semaphore.release();
    });

    semaphore.acquire();
    REQUIRE(produced.load());
    producer.join();
}

TEST_CASE("latch: wait returns after all arrivals", "[sync]")
{
    Bonostl::latch latch(4);
    std::atomic<int> started{0};

    std::vector<std::thread> workers;
    for (int t = 0; t < 4; ++t)
    {
        workers.emplace_back([&] {
            started.fetch_add(1, std::memory_order_acq_rel);
            latch.count_down();
        });
    }

    latch.wait();
    REQUIRE(started.load() == 4);

    for (auto& worker : workers)
    {
        worker.join();
    }
}

TEST_CASE("latch: try_wait before and after", "[sync]")
{
    Bonostl::latch latch(1);

    REQUIRE_FALSE(latch.try_wait());
    latch.count_down();
    REQUIRE(latch.try_wait());
}

TEST_CASE("barrier: phases align across threads", "[sync]")
{
    constexpr int thread_count = 4;
    constexpr int phases = 3;

    std::atomic<int> completions{0};
    Bonostl::barrier barrier(thread_count, [&] { ++completions; });
    std::vector<std::atomic<int>> phase_of(thread_count);
    for (auto& phase : phase_of)
    {
        phase.store(0);
    }

    std::vector<std::thread> threads;
    for (int t = 0; t < thread_count; ++t)
    {
        threads.emplace_back([&, t] {
            for (int p = 1; p <= phases; ++p)
            {
                phase_of[t].store(p, std::memory_order_release);
                barrier.arrive_and_wait();

                // After the barrier, every thread must have reached phase p.
                for (int j = 0; j < thread_count; ++j)
                {
                    REQUIRE(phase_of[j].load(std::memory_order_acquire) >= p);
                }
            }
        });
    }
    for (auto& thread : threads)
    {
        thread.join();
    }

    REQUIRE(completions.load() == phases);
}
