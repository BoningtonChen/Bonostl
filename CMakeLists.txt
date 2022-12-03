cmake_minimum_required(VERSION 3.24)
project(Bonostl)

set(CMAKE_CXX_STANDARD 23)

set(INC_DIR include/)
set(SRC_DIR src/)

set(TEST_SRC
        ${SRC_DIR}main.cpp
        )
set(INC_SRC
        ${INC_DIR}bonostlpch.h
        ${INC_DIR}queue.hpp
        ${INC_DIR}threadsafe_stack.hpp
        ${INC_DIR}threadsafe_queue.hpp
        )

add_executable(Bonostl ${TEST_SRC} ${INC_SRC})
