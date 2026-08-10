set_project("Bonostl")
set_version("2.0.0")
set_languages("cxx26")

add_rules("mode.debug", "mode.release")

option("demo", {default = true, description = "Build the Bonostl demo program"})
option("tests", {default = true, description = "Build the Bonostl test suite"})

target("Bonostl")
    set_kind("headeronly")
    add_includedirs("include", {public = true})

if has_config("demo") then
    target("bonostl_demo")
        set_kind("binary")
        add_deps("Bonostl")
        add_files("src/main.cpp")
end
