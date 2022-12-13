cmake_minimum_required(VERSION 3.24)
project(Bonostl CXX)

set(CMAKE_CXX_STANDARD 23)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++23 -O2")

include_directories(include/)

set(INC_DIR include/)
set(SRC_DIR src/)

set(TEST_SRC
        ${SRC_DIR}main.cpp
        )

set(INC_SRC
        ${INC_DIR}bonostlpch.h

        ${INC_DIR}join_threads.hpp
        ${INC_DIR}spinlock_mutex.hpp

        ${INC_DIR}queue.hpp
        ${INC_DIR}threadsafe_stack.hpp
        ${INC_DIR}threadsafe_queue.hpp
        ${INC_DIR}threadsafe_lookup_table.hpp
        ${INC_DIR}threadsafe_list.hpp

        ${INC_DIR}lockfree_stack.hpp
        ${INC_DIR}lockfree_queue.hpp

        ${INC_DIR}parallel_for_each.hpp
        ${INC_DIR}parallel_find.hpp
        ${INC_DIR}parallel_partial_sum.hpp
        ${INC_DIR}parallel_quick_sort.hpp
        )

add_executable(Bonostl ${INC_SRC} ${TEST_SRC})

target_precompile_headers(Bonostl PUBLIC ${INC_DIR}bonostlpch.h)
