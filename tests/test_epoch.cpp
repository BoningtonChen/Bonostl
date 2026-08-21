#include <catch_amalgamated.hpp>

#include <atomic>
#include <thread>
#include <vector>

#include "epoch_reclaim.hpp"

namespace
{
    struct counted_node
    {
        static std::atomic<int> alive;

        explicit counted_node(int v)
            : value(v)
        {
            alive.fetch_add(1, std::memory_order_relaxed);
        }

        ~counted_node()
        {
            alive.fetch_sub(1, std::memory_order_relaxed);
        }

        int value;
    };

    std::atomic<int> counted_node::alive{0};
}

TEST_CASE("epoch_domain: active guard blocks reclamation", "[epoch]")
{
    Bonostl::epoch_domain domain;

    {
        Bonostl::epoch_domain::guard guard(domain);
        domain.retire(new counted_node(1));
        domain.collect();

        // The guard announced its epoch before the retire, so the node must
        // stay alive while the guard is in scope.
        REQUIRE(counted_node::alive.load() == 1);
    }

    domain.collect();
    REQUIRE(counted_node::alive.load() == 0);
}

TEST_CASE("epoch_domain: retire without active guards reclaims immediately", "[epoch]")
{
    Bonostl::epoch_domain domain;

    domain.retire(new counted_node(1));
    domain.retire(new counted_node(2));
    domain.collect();

    REQUIRE(counted_node::alive.load() == 0);
}

TEST_CASE("epoch_domain: concurrent retire and reclaim", "[epoch]")
{
    Bonostl::epoch_domain domain;

    constexpr int per_thread = 200;
    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t)
    {
        threads.emplace_back([&] {
            for (int i = 0; i < per_thread; ++i)
            {
                Bonostl::epoch_domain::guard guard(domain);
                domain.retire(new counted_node(i));

                if (i % 16 == 0)
                {
                    domain.collect();
                }
            }
        });
    }
    for (auto& thread : threads)
    {
        thread.join();
    }

    domain.collect();
    REQUIRE(counted_node::alive.load() == 0);
}
