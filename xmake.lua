set_project("coke-postgres")

add_rules("mode.debug", "mode.release")
add_rules("plugin.compile_commands.autoupdate", {outputdir = "build"})

set_languages("cxx20")

add_repositories("t-xrepo https://github.com/Tenire/xrepo.git")
-- add_repositories("local-repo .")

add_requires("wf_postgres")
add_requires("coke")

target("coke_postgres")
    set_kind("static")
    add_files("src/*.cpp")
    add_includedirs("include", {public = true})
    add_headerfiles("include/(**.h)")
    add_packages("wf_postgres", "coke", {public = true})

includes("tutorial")
includes("test")
