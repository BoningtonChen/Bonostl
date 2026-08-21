#include <catch_amalgamated.hpp>

#include <algorithm>
#include <functional>
#include <list>
#include <numeric>
#include <random>
#include <vector>

#include "parallel_accumulate.hpp"
#include "parallel_find.hpp"
#include "parallel_for_each.hpp"
#include "parallel_partial_sum.hpp"
#include "parallel_quick_sort.hpp"

namespace
{
    std::vector<int> make_random_vector(std::size_t size, int seed)
    {
        std::mt19937 rng(seed);
        std::vector<int> data(size);
        std::ranges::generate(data, [&] { return static_cast<int>(rng() % 1000); });
        return data;
    }
}

TEST_CASE("parallel_find: finds existing value", "[parallel]")
{
    std::vector<int> data = make_random_vector(1000, 1);
    data[777] = -42;

    auto const found = Bonostl::parallel_find(data.begin(), data.end(), -42);
    REQUIRE(found != data.end());
    REQUIRE(*found == -42);
}

TEST_CASE("parallel_find: missing value returns end", "[parallel]")
{
    std::vector<int> data = make_random_vector(1000, 2);

    auto const not_found = Bonostl::parallel_find(data.begin(), data.end(), -1);
    REQUIRE(not_found == data.end());
}

TEST_CASE("parallel_find: empty and single-element ranges", "[parallel]")
{
    std::vector<int> data{42};

    REQUIRE(Bonostl::parallel_find(data.begin(), data.begin(), 42) == data.begin());
    REQUIRE(Bonostl::parallel_find(data.begin(), data.end(), 42) == data.begin());
    REQUIRE(Bonostl::parallel_find(data.begin(), data.end(), 0) == data.end());
}

TEST_CASE("parallel_for_each: matches std::for_each", "[parallel]")
{
    std::vector<int> data = make_random_vector(5000, 3);
    std::vector<int> reference = data;

    Bonostl::parallel_for_each(data.begin(), data.end(), [](int& v) { v = v * 2 + 1; });
    std::ranges::for_each(reference, [](int& v) { v = v * 2 + 1; });

    REQUIRE(data == reference);
}

TEST_CASE("parallel_for_each: empty range is a no-op", "[parallel]")
{
    std::vector<int> data;
    Bonostl::parallel_for_each(data.begin(), data.end(), [](int&) { FAIL("must not run"); });
}

TEST_CASE("parallel_partial_sum: matches std::partial_sum", "[parallel]")
{
    std::vector<int> data = make_random_vector(3000, 4);

    std::vector<int> reference = data;
    std::partial_sum(reference.begin(), reference.end(), reference.begin());

    Bonostl::parallel_partial_sum(data.begin(), data.end());

    REQUIRE(data == reference);
}

TEST_CASE("parallel_partial_sum: edge cases", "[parallel]")
{
    std::vector<int> empty;
    Bonostl::parallel_partial_sum(empty.begin(), empty.end());

    std::vector<int> single{7};
    Bonostl::parallel_partial_sum(single.begin(), single.end());
    REQUIRE(single[0] == 7);

    std::vector<int> small{1, 2, 3};
    Bonostl::parallel_partial_sum(small.begin(), small.end());
    REQUIRE(small == std::vector<int>({1, 3, 6}));
}

TEST_CASE("parallel_quick_sort: sorts random lists", "[parallel]")
{
    std::mt19937 rng(5);
    std::list<int> data;
    for (int i = 0; i < 10000; ++i)
    {
        data.push_back(static_cast<int>(rng() % 10000));
    }

    std::list<int> reference = data;
    reference.sort();

    auto const sorted = Bonostl::parallel_quick_sort(std::move(data));
    REQUIRE(sorted == reference);
}

TEST_CASE("parallel_quick_sort: handles duplicates and edge sizes", "[parallel]")
{
    std::list<int> duplicates(1000, 5);
    auto const sorted_dup = Bonostl::parallel_quick_sort(std::move(duplicates));
    REQUIRE(sorted_dup.size() == 1000);
    REQUIRE(std::ranges::all_of(sorted_dup, [](int v) { return v == 5; }));

    std::list<int> single{1};
    REQUIRE(Bonostl::parallel_quick_sort(std::move(single)) == std::list<int>({1}));

    std::list<int> empty;
    REQUIRE(Bonostl::parallel_quick_sort(std::move(empty)).empty());
}

TEST_CASE("parallel_accumulate: matches std::accumulate on large random vector", "[parallel]")
{
    std::vector<int> data = make_random_vector(10000, 6);

    int const expected = std::accumulate(data.begin(), data.end(), 0);
    int const actual = Bonostl::parallel_accumulate(data.begin(), data.end(), 0);

    REQUIRE(actual == expected);
}

TEST_CASE("parallel_accumulate: empty range returns init", "[parallel]")
{
    std::vector<int> empty;

    REQUIRE(Bonostl::parallel_accumulate(empty.begin(), empty.end(), 42) == 42);
}

TEST_CASE("parallel_accumulate: custom binary op", "[parallel]")
{
    std::vector<int> data = make_random_vector(5000, 7);

    int const expected = std::accumulate(data.begin(), data.end(), 1, std::multiplies<>());
    int const actual =
        Bonostl::parallel_accumulate(data.begin(), data.end(), 1, std::multiplies<>());

    REQUIRE(actual == expected);
}

TEST_CASE("parallel_accumulate: small range below 2*min_per_thread", "[parallel]")
{
    std::vector<int> data = make_random_vector(30, 8);

    int const expected = std::accumulate(data.begin(), data.end(), 0);
    int const actual = Bonostl::parallel_accumulate(data.begin(), data.end(), 0);

    REQUIRE(actual == expected);
}
