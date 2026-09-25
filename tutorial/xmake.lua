option("tutorial")
    set_default(false)
    set_showmenu(true)
    set_description("Build tutorial examples")

if has_config("tutorial") then
    target("postgres_cli")
        set_kind("binary")
        add_files("postgres_cli.cc")
        add_deps("coke_postgres")
        add_packages("wf_postgres", "coke")

    target("tutorial_params")
        set_kind("binary")
        add_files("tutorial_params.cc")
        add_deps("coke_postgres")
        add_packages("wf_postgres", "coke")
end
