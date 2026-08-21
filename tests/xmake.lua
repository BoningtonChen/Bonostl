target("bonostl_tests")
    set_kind("binary")
    add_deps("Bonostl")
    add_includedirs("third_party")
    add_files(
        "test_queue.cpp",
        "test_lockfree.cpp",
        "test_parallel.cpp",
        "test_threadpool.cpp",
        "third_party/catch_amalgamated.cpp")
    on_run(function (target)
        os.exec(target:targetfile())
    end)
