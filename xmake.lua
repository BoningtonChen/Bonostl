set_project("Bonostl")
set_version("2.0.0")
set_languages("cxx26")

add_rules("mode.debug", "mode.release")

option("benchmark", {default = true, description = "Build the Bonostl benchmark program"})
option("tests", {default = true, description = "Build the Bonostl test suite"})

target("Bonostl")
    set_kind("headeronly")
    add_includedirs("include", {public = true})
    add_headerfiles("include/*.h", "include/*.hpp")
    if is_plat("linux") or is_config("toolchain", "gcc", "clang") then
        add_syslinks("atomic", {public = true})
    end

if has_config("benchmark") then
    target("bonostl_benchmark")
        set_kind("binary")
        add_deps("Bonostl")
        add_files("src/main.cpp")
end

if has_config("tests") then
    includes("tests/xmake.lua")
end
