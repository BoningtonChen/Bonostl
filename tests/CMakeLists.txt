add_executable(bonostl_tests
    test_queue.cpp
    test_lockfree.cpp
    test_parallel.cpp
    third_party/catch_amalgamated.cpp)

target_link_libraries(bonostl_tests PRIVATE Bonostl::Bonostl)
target_include_directories(bonostl_tests PRIVATE third_party)

add_test(NAME bonostl_tests COMMAND bonostl_tests)
