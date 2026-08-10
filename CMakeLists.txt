cmake_minimum_required(VERSION 3.28)

project(Bonostl
        VERSION 2.0.0
        DESCRIPTION "A header-only C++26 concurrency library"
        LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(BONOSTL_BUILD_DEMO "Build the Bonostl demo program" ON)

add_library(Bonostl INTERFACE)
add_library(Bonostl::Bonostl ALIAS Bonostl)

target_include_directories(Bonostl INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)

if(MSVC)
    target_compile_options(Bonostl INTERFACE /W4 /permissive-)
else()
    target_compile_options(Bonostl INTERFACE -Wall -Wextra)
endif()

if(BONOSTL_BUILD_DEMO)
    add_executable(bonostl_demo src/main.cpp)
    target_link_libraries(bonostl_demo PRIVATE Bonostl::Bonostl)
    target_precompile_headers(bonostl_demo PRIVATE include/bonostlpch.h)
endif()

include(GNUInstallDirs)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(TARGETS Bonostl EXPORT BonostlTargets)
install(EXPORT BonostlTargets
        FILE BonostlConfig.cmake
        NAMESPACE Bonostl::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Bonostl)
