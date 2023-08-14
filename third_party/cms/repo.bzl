def _cms_new_local_repository(ctx):
    cmd = ["mkdir", "-p", ctx.path(".")]
    ctx.execute(cmd)

    cms_root_file = ctx.os.environ["TCMALLOC_CMS_EXTERNALS"]
    cmd = ["cat", cms_root_file]
    res = ctx.execute(cmd)
    cms_path = [
        line.split(":", 1)[1]
        for line in res.stdout.split("\n")
        if line.startswith(ctx.attr.name + ":")
    ][-1].strip()

    if cms_path:
        cmd = ["ls", cms_path + "/"]
        res = ctx.execute(cmd)
        for rfile in res.stdout.split("\n"):
            if rfile:
                ctx.symlink(cms_path + "/" + rfile, ctx.path(rfile))

    ctx.template("BUILD.bazel", ctx.attr.build_file, {
        "%prefix%": "external",
    }, False)

    for xfile in ctx.attr.create_files:
        ctx.file(xfile, content="")

    for src, dest in ctx.attr.symlinks.items():
        if src.startswith("//") or src.startswith("@"):
            src = Label(src)
        ctx.symlink(src, dest)

cms_new_local_repository = repository_rule(
    implementation = _cms_new_local_repository,
    attrs = {
        "build_file": attr.label(),
        "symlinks": attr.string_dict(),
        "create_files": attr.string_list(),
    },
    environ = [
        "TC_CMS_EXTERNALS",
    ],
)
