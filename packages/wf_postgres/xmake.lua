package("wf_postgres")
    add_deps("workflow", "openssl")
    add_urls("https://github.com/tenire/wf-postgres.git")
    on_install("linux", "macosx", function (package)
        import("package.tools.xmake").install(package)
    end)
package_end()

add_requires("wf_postgres", "coke")
