#include <catch_amalgamated.hpp>

#include <functional>
#include <memory>

#include "function_wrapper.hpp"

TEST_CASE("function_wrapper: invokes wrapped callable", "[threadpool]")
{
    bool called = false;
    Bonostl::function_wrapper task([&] { called = true; });

    task();

    REQUIRE(called);
}

TEST_CASE("function_wrapper: wraps move-only callable", "[threadpool]")
{
    auto resource = std::make_unique<int>(42);
    int observed = 0;

    Bonostl::function_wrapper task([res = std::move(resource), &observed] { observed = *res; });
    task();

    REQUIRE(observed == 42);
}

TEST_CASE("function_wrapper: move construction transfers ownership", "[threadpool]")
{
    int count = 0;
    Bonostl::function_wrapper first([&] { ++count; });

    Bonostl::function_wrapper second(std::move(first));
    second();

    REQUIRE(count == 1);
}

TEST_CASE("function_wrapper: default construction then move assignment", "[threadpool]")
{
    int count = 0;
    Bonostl::function_wrapper task;

    task = Bonostl::function_wrapper([&] { count += 5; });
    task();

    REQUIRE(count == 5);
}

TEST_CASE("function_wrapper: calling empty wrapper throws", "[threadpool]")
{
    Bonostl::function_wrapper task;

    REQUIRE_THROWS_AS(task(), std::bad_function_call);
}
