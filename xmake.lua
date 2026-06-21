set_project("coke-postgres")

add_rules("mode.debug", "mode.release")
add_rules("plugin.compile_commands.autoupdate", {outputdir = "build"})

set_languages("cxx20")

includes("packages/coke/xmake.lua")
includes("packages/wf_postgres/xmake.lua")

target("coke_postgres")
    set_kind("static")
    add_files("src/*.cpp")
    add_includedirs("include", {public = true})
    add_headerfiles("include/(**.h)")
    add_packages("wf_postgres", "coke", {public = true})

includes("tutorial")
includes("test")
