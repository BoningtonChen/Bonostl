#include <iostream>

#include "bonostlpch.h"

#include "join_threads.hpp"
#include "lockfree_queue.hpp"
#include "lockfree_stack.hpp"
#include "parallel_find.hpp"
#include "parallel_for_each.hpp"
#include "parallel_partial_sum.hpp"
#include "parallel_quick_sort.hpp"
#include "queue.hpp"
#include "spinlock_mutex.hpp"
#include "threadsafe_list.hpp"
#include "threadsafe_lookup_table.hpp"
#include "threadsafe_queue.hpp"
#include "threadsafe_stack.hpp"

namespace
{
    void demo_containers()
    {
        std::cout << "== containers ==\n";

        Bonostl::queue<int> q;
        q.push(1);
        q.push(2);
        std::cout << "queue try_pop: " << *q.try_pop() << '\n';

        Bonostl::threadsafe_stack<int> stack;
        stack.push(10);
        stack.push(20);
        std::cout << "threadsafe_stack pop: " << *stack.pop() << '\n';

        Bonostl::threadsafe_queue<int> tq;
        tq.push(100);
        std::cout << "threadsafe_queue wait_and_pop: " << tq.wait_and_pop() << '\n';

        Bonostl::threadsafe_lookup_table<int, std::string> table;
        table.add_or_update_mapping(1, "one");
        table.add_or_update_mapping(2, "two");
        std::cout << "threadsafe_lookup_table value_for(1): " << *table.value_for(1) << '\n';

        Bonostl::threadsafe_list<int> list;
        list.push_front(5);
        list.push_front(6);
        list.for_each([](int v) { std::cout << "threadsafe_list item: " << v << '\n'; });
    }

    void demo_lockfree()
    {
        std::cout << "== lock-free ==\n";

        Bonostl::lockfree_stack<int> stack;
        stack.push(1);
        stack.push(2);
        std::cout << "lockfree_stack pop: " << *stack.pop() << '\n';

        Bonostl::lockfree_queue<int> queue;
        queue.push(3);
        queue.push(4);
        std::cout << "lockfree_queue pop: " << *queue.pop() << '\n';
    }

    void demo_spinlock()
    {
        Bonostl::spinlock_mutex mutex;
        int counter = 0;

        std::vector<std::thread> threads;
        for (int i = 0; i < 4; ++i)
        {
            threads.emplace_back([&] {
                for (int j = 0; j < 1000; ++j)
                {
                    std::lock_guard lock(mutex);
                    ++counter;
                }
            });
        }
        {
            Bonostl::join_threads joiner(threads);
        }

        std::cout << "spinlock_mutex guarded counter: " << counter << '\n';
    }

    void demo_algorithms()
    {
        std::cout << "== algorithms ==\n";

        std::vector<int> data(1000);
        for (int i = 0; i < 1000; ++i)
        {
            data[i] = i;
        }

        auto const found = Bonostl::parallel_find(data.begin(), data.end(), 500);
        std::cout << "parallel_find(500): " << (found != data.end() ? std::to_string(*found) : "not found") << '\n';

        Bonostl::parallel_for_each(data.begin(), data.end(), [](int& v) { v *= 2; });
        std::cout << "parallel_for_each data[999]: " << data[999] << '\n';

        std::vector<int> sums(100, 1);
        Bonostl::parallel_partial_sum(sums.begin(), sums.end());
        std::cout << "parallel_partial_sum last: " << sums.back() << '\n';

        std::list<int> unsorted{5, 3, 8, 1, 9, 2, 7, 4, 6, 0};
        auto const sorted = Bonostl::parallel_quick_sort(unsorted);
        std::cout << "parallel_quick_sort: ";
        for (int v : sorted)
        {
            std::cout << v << ' ';
        }
        std::cout << '\n';
    }
}

int main()
{
    demo_containers();
    demo_lockfree();
    demo_spinlock();
    demo_algorithms();

    std::cout << "Build Complete!\n";
    return 0;
}
