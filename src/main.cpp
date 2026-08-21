#include <algorithm>
#include <chrono>
#include <cstdint>
#include <functional>
#include <iomanip>
#include <iostream>
#include <list>
#include <numeric>
#include <string>
#include <vector>

#include "bonostlpch.h"

#include "parallel_accumulate.hpp"
#include "parallel_find.hpp"
#include "parallel_for_each.hpp"
#include "parallel_merge_sort.hpp"
#include "parallel_partial_sum.hpp"
#include "parallel_predicates.hpp"
#include "parallel_quick_sort.hpp"
#include "parallel_scan.hpp"
#include "parallel_transform.hpp"

namespace
{
    using clock_type = std::chrono::steady_clock;

    // Sink for timed results: keeps the optimizer from eliminating the work.
    std::int64_t g_checksum = 0;

    // One untimed warm-up round (thread-pool spin-up, caches, branch
    // predictors), then `rounds` timed samples; the reported value is the
    // MEDIAN, robust against scheduler noise and outliers.
    double measure_ms(std::function<void()> const& fn, int rounds = 5)
    {
        fn();

        std::vector<double> samples;
        samples.reserve(rounds);
        for (int round = 0; round < rounds; ++round)
        {
            auto const start = clock_type::now();
            fn();
            auto const end = clock_type::now();
            samples.push_back(std::chrono::duration<double, std::milli>(end - start).count());
        }

        std::sort(samples.begin(), samples.end());
        return samples[samples.size() / 2];
    }

    struct benchmark_result
    {
        std::string name;
        std::string workload;
        double sequential_ms;
        double parallel_ms;
    };

    std::vector<benchmark_result> g_results;

    void run_benchmark(std::string name, std::string workload,
                       std::function<void()> sequential, std::function<void()> parallel)
    {
        double const seq_ms = measure_ms(sequential);
        double const par_ms = measure_ms(parallel);
        g_results.push_back({std::move(name), std::move(workload), seq_ms, par_ms});
    }

    void print_mismatch(std::string const& name)
    {
        std::cout << "  [ERROR] " << name << " result mismatch - benchmark skipped\n";
    }

    std::string format_count(std::size_t n)
    {
        if (n >= 1000000)
        {
            return std::to_string(n / 1000000) + "M";
        }
        if (n >= 1000)
        {
            return std::to_string(n / 1000) + "k";
        }
        return std::to_string(n);
    }

    // Pseudo-random permutation via multiplicative hashing: keeps sorts and
    // scans on non-trivial input instead of already-sorted data.
    std::vector<std::int64_t> make_data(std::size_t n, std::int64_t mod)
    {
        std::vector<std::int64_t> data(n);
        for (std::size_t i = 0; i < n; ++i)
        {
            data[i] = static_cast<std::int64_t>(
                (i * 2654435761ull) % static_cast<std::uint64_t>(mod));
        }
        return data;
    }

    void bench_accumulate(std::size_t n)
    {
        auto const data = make_data(n, 1000);

        std::int64_t const expected = std::accumulate(data.begin(), data.end(), std::int64_t{0});
        std::int64_t const actual =
            Bonostl::parallel_accumulate(data.begin(), data.end(), std::int64_t{0});
        if (actual != expected)
        {
            return print_mismatch("parallel_accumulate");
        }

        run_benchmark(
            "parallel_accumulate", format_count(n) + " int64 sum",
            [&data] { g_checksum += std::accumulate(data.begin(), data.end(), std::int64_t{0}); },
            [&data] {
                g_checksum += Bonostl::parallel_accumulate(data.begin(), data.end(), std::int64_t{0});
            });
    }

    void bench_for_each(std::size_t n)
    {
        auto data = make_data(n, 1000);
        auto work_seq = data;
        auto work_par = data;

        auto const op = [](std::int64_t& v) { v ^= 0x5A5A5A5A; };
        std::for_each(work_seq.begin(), work_seq.end(), op);
        Bonostl::parallel_for_each(work_par.begin(), work_par.end(), op);
        if (work_seq != work_par)
        {
            return print_mismatch("parallel_for_each");
        }

        // The XOR op is bitwise and data-independent, so each timed round
        // performs identical work regardless of the values it toggles.
        run_benchmark(
            "parallel_for_each", format_count(n) + " int64 xor",
            [&data, &op] { std::for_each(data.begin(), data.end(), op); g_checksum += data.back(); },
            [&data, &op] {
                Bonostl::parallel_for_each(data.begin(), data.end(), op);
                g_checksum += data.back();
            });
    }

    void bench_transform(std::size_t n)
    {
        auto const data = make_data(n, 1000);
        std::vector<std::int64_t> out_seq(n);
        std::vector<std::int64_t> out_par(n);

        auto const op = [](std::int64_t v) { return v * 3 + 1; };
        std::transform(data.begin(), data.end(), out_seq.begin(), op);
        Bonostl::parallel_transform(data.begin(), data.end(), out_par.begin(), op);
        if (out_seq != out_par)
        {
            return print_mismatch("parallel_transform");
        }

        run_benchmark(
            "parallel_transform", format_count(n) + " int64 map",
            [&] {
                std::transform(data.begin(), data.end(), out_seq.begin(), op);
                g_checksum += out_seq.back();
            },
            [&] {
                Bonostl::parallel_transform(data.begin(), data.end(), out_par.begin(), op);
                g_checksum += out_par.back();
            });
    }

    void bench_count_if(std::size_t n)
    {
        auto const data = make_data(n, 1000);

        auto const pred = [](std::int64_t v) { return v < 500; };
        std::size_t const expected =
            static_cast<std::size_t>(std::count_if(data.begin(), data.end(), pred));
        std::size_t const actual = Bonostl::parallel_count_if(data.begin(), data.end(), pred);
        if (actual != expected)
        {
            return print_mismatch("parallel_count_if");
        }

        run_benchmark(
            "parallel_count_if", format_count(n) + " int64 (v < 500)",
            [&] { g_checksum += std::count_if(data.begin(), data.end(), pred); },
            [&] { g_checksum += Bonostl::parallel_count_if(data.begin(), data.end(), pred); });
    }

    void bench_find(std::size_t n)
    {
        std::vector<std::int64_t> data(n, 1);
        data[n * 9 / 10] = 42;  // worst-ish case: hit near the end

        auto const expected = std::find(data.begin(), data.end(), 42);
        auto const actual = Bonostl::parallel_find(data.begin(), data.end(), 42);
        if (actual != expected)
        {
            return print_mismatch("parallel_find");
        }

        run_benchmark(
            "parallel_find", format_count(n) + " int64 (hit 90%)",
            [&data] { g_checksum += *std::find(data.begin(), data.end(), 42); },
            [&data] { g_checksum += *Bonostl::parallel_find(data.begin(), data.end(), 42); });
    }

    void bench_exclusive_scan(std::size_t n)
    {
        auto const data = make_data(n, 1000);
        std::vector<std::int64_t> out_seq(n);
        std::vector<std::int64_t> out_par(n);

        std::exclusive_scan(data.begin(), data.end(), out_seq.begin(), std::int64_t{0});
        Bonostl::parallel_exclusive_scan(data.begin(), data.end(), out_par.begin(),
                                         std::int64_t{0});
        if (out_seq != out_par)
        {
            return print_mismatch("parallel_exclusive_scan");
        }

        run_benchmark(
            "parallel_exclusive_scan", format_count(n) + " int64 prefix",
            [&] {
                std::exclusive_scan(data.begin(), data.end(), out_seq.begin(), std::int64_t{0});
                g_checksum += out_seq.back();
            },
            [&] {
                Bonostl::parallel_exclusive_scan(data.begin(), data.end(), out_par.begin(),
                                                 std::int64_t{0});
                g_checksum += out_par.back();
            });
    }

    void bench_partial_sum(std::size_t n)
    {
        auto const pristine = make_data(n, 1000);
        std::vector<std::int64_t> work_seq;
        std::vector<std::int64_t> work_par;

        work_seq = pristine;
        std::partial_sum(work_seq.begin(), work_seq.end(), work_seq.begin());
        work_par = pristine;
        Bonostl::parallel_partial_sum(work_par.begin(), work_par.end());
        if (work_seq != work_par)
        {
            return print_mismatch("parallel_partial_sum");
        }

        // Each timed round starts from the pristine input so the partial sums
        // cannot compound across rounds.
        run_benchmark(
            "parallel_partial_sum", format_count(n) + " int64 in-place",
            [&] {
                work_seq = pristine;
                std::partial_sum(work_seq.begin(), work_seq.end(), work_seq.begin());
                g_checksum += work_seq.back();
            },
            [&] {
                work_par = pristine;
                Bonostl::parallel_partial_sum(work_par.begin(), work_par.end());
                g_checksum += work_par.back();
            });
    }

    void bench_merge_sort(std::size_t n)
    {
        auto const pristine = make_data(n, static_cast<std::int64_t>(n));
        std::vector<std::int64_t> work_seq;
        std::vector<std::int64_t> work_par;

        work_seq = pristine;
        std::stable_sort(work_seq.begin(), work_seq.end());
        work_par = pristine;
        Bonostl::parallel_merge_sort(work_par.begin(), work_par.end());
        if (work_seq != work_par)
        {
            return print_mismatch("parallel_merge_sort");
        }

        run_benchmark(
            "parallel_merge_sort", format_count(n) + " int64 stable sort",
            [&] {
                work_seq = pristine;
                std::stable_sort(work_seq.begin(), work_seq.end());
                g_checksum += work_seq.back();
            },
            [&] {
                work_par = pristine;
                Bonostl::parallel_merge_sort(work_par.begin(), work_par.end());
                g_checksum += work_par.back();
            });
    }

    void bench_quick_sort(std::size_t n)
    {
        std::list<std::int64_t> pristine;
        auto const perm = make_data(n, static_cast<std::int64_t>(n));
        for (std::int64_t v : perm)
        {
            pristine.push_back(v);
        }

        auto check_seq = pristine;
        check_seq.sort();
        auto const check_par = Bonostl::parallel_quick_sort(pristine);
        if (check_seq != check_par)
        {
            return print_mismatch("parallel_quick_sort");
        }

        run_benchmark(
            "parallel_quick_sort", format_count(n) + " list nodes",
            [&pristine] {
                auto list = pristine;
                list.sort();
                g_checksum += list.back();
            },
            [&pristine] {
                auto list = pristine;
                auto sorted = Bonostl::parallel_quick_sort(std::move(list));
                g_checksum += sorted.back();
            });
    }

    void print_results()
    {
        std::cout << "\n" << std::left << std::setw(26) << "algorithm" << std::setw(22)
                  << "workload" << std::right << std::setw(12) << "sequential" << std::setw(12)
                  << "parallel" << std::setw(10) << "speedup" << '\n';
        std::cout << std::string(82, '-') << '\n';

        for (auto const& result : g_results)
        {
            double const speedup =
                result.parallel_ms > 0.0 ? result.sequential_ms / result.parallel_ms : 0.0;
            std::cout << std::left << std::setw(26) << result.name << std::setw(22)
                      << result.workload << std::right << std::fixed << std::setprecision(2)
                      << std::setw(10) << result.sequential_ms << " ms" << std::setw(10)
                      << result.parallel_ms << " ms" << std::setw(9) << std::setprecision(2)
                      << speedup << "x\n";
        }
    }
}  // namespace

int main()
{
    std::cout << "== Bonostl benchmark ==\n";
    std::cout << "CPU threads (hardware_concurrency): " << std::thread::hardware_concurrency()
              << '\n';
    std::cout << "Method: median of 5 timed rounds after 1 warm-up; results verified against "
                 "the std baseline before timing\n";

    std::size_t const n = 5000000;
    bench_accumulate(n);
    bench_for_each(n);
    bench_transform(n);
    bench_count_if(n);
    bench_find(n);
    bench_exclusive_scan(n);
    bench_partial_sum(n);
    bench_merge_sort(500000);
    bench_quick_sort(300000);

    print_results();
    std::cout << "\nchecksum (dead-code guard): " << g_checksum << '\n';
    return 0;
}
