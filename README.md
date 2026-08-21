# Bonostl
![Static Badge](https://img.shields.io/badge/Language-C%2B%2B26-blue?logo=cplusplus)
![Static Badge](https://img.shields.io/badge/Built_by-CMake-darkgreen?logo=Cmake)
![Static Badge](https://img.shields.io/badge/Built_by-xmake-blue?logo=xmake)
![Static Badge](https://img.shields.io/badge/License-MIT-green)


## Content
<!-- TOC -->
* [Bonostl](#bonostl)
  * [Content](#content)
  * [Description](#description)
  * [Get Started](#get-started)
  * [Build](#build)
  * [Library Stuff](#library-stuff)
  * [LICENSE](#license)
  * [Copyright](#copyright)
<!-- TOC -->

## Description
A Bonity's C++ standard library — a header-only concurrency library implemented in C++26,
covering parallel algorithms, thread-safe containers and lock-free data structures.

## Get Started
- You can use `git clone https://github.com/BoningtonChen/Bonostl` to clone the repository.
- The Bonostl library headers are all `.hpp` headers, which means you can simply include them in your own projects.

## Build
The project supports both CMake and xmake build systems. It requires a C++26 compiler
and links libatomic for 16-byte atomics on GCC/Clang.

### CMake
```bash
cmake -B build -G "MinGW Makefiles" -DCMAKE_CXX_COMPILER=g++
cmake --build build
./build/bonostl_demo.exe
ctest --test-dir build          # run the test suite
```

### xmake
```bash
xmake f --toolchain=gcc
xmake
./build/windows/x64/release/bonostl_demo.exe
xmake run bonostl_tests         # run the test suite
```

### Visual Studio
Both build systems work inside Visual Studio without changing the toolchain
(MinGW GCC is still the compiler in both cases):

- **CMake (Open Folder)**: `File > Open > Folder...` on the project root.
  Visual Studio reads `CMakePresets.json` and offers the `mingw-debug` /
  `mingw-release` presets for configure, build and test (CTest).
- **xmake (solution)**: open `vsxmake2026/Bonostl.slnx`. The projects wrap
  xmake commands, so Build/Rebuild/Clean run `xmake` under the hood.
  Regenerate after changing `xmake.lua`:
  ```bash
  xmake project -k vsxmake2026 -m "debug,release"
  dotnet sln vsxmake2026/Bonostl.sln migrate   # upgrade to .slnx
  ```
  Set `bonostl_demo` (or `bonostl_tests`) as the startup project before
  running — the `Bonostl` target is a header-only library with no executable.

## Testing
The test suite uses Catch2 v3 and covers:
- FIFO/LIFO ordering and empty states for all containers
- Concurrent producer/consumer invariants (element counts and value sums)
- Lock-free stack/queue stress tests with multiple threads
- Parallel algorithms verified against their `std::` counterparts

## Library Stuff
- bonostlpch(A dependency of a bunch of files included from C++ standard libraries)
- Containers 
  - queue (single-threaded; use threadsafe_queue or lockfree_queue for concurrency)
  - threadsafe_stack
  - threadsafe_queue
  - blocking_queue (bounded, blocking push/pop for backpressure)
  - threadsafe_lookup_table
  - threadsafe_list
  - lockfree_stack
  - lockfree_queue
- Algorithms
  - parallel_accumulate
  - parallel_find
  - parallel_for_each
  - parallel_partial_sum
  - parallel_quick_sort
  - parallel_transform
  - parallel_predicates (count_if, all_of, any_of, none_of)
  - parallel_scan (inclusive and exclusive prefix sums)
  - parallel_merge_sort (stable)
- Locks
  - spinlock_mutex
  - shared_spinlock (reader-writer spin lock; std::shared_lock compatible)
  - seqlock (read-optimistic; readers never block)
- Utilities
  - hazard_ptr (hazard-pointer reclamation helpers for lock-free containers)
- Thread pool
  - function_wrapper (move-only type-erased task wrapper)
  - work_stealing_queue
  - thread_pool (work-stealing thread pool with per-thread local queues)

> Note: `pop`-style operations return `std::optional<T>` instead of throwing or
> returning null pointers. `threadsafe_lookup_table::value_for` also returns
> `std::optional<Value>`.

## LICENSE
Bonostl uses MIT License.
```asciidoc
MIT License

Copyright (c) 2025 Cle2ment

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

```

## Copyright
© Cle2ment, 2025
