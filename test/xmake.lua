option("tests")
    set_default(false)
    set_showmenu(true)
    set_description("Build unit tests")

if has_config("tests") then
    target("test_postgres")
        set_kind("binary")
        add_files("test_postgres.cc")
        add_deps("coke_postgres")
        add_packages("wf_postgres", "coke")
end
