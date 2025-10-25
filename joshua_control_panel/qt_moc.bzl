def _qt_moc_impl(ctx):
    """Generate Qt MOC sources from headers with Q_OBJECT.

    This rule runs the Qt `moc` tool on each header and emits a corresponding
    C++ source file with the suffix `.moc.cc`.
    """
    outputs = []
    for header in ctx.files.hdrs:
        stem = header.basename.rsplit(".", 1)[0]
        out_src = ctx.actions.declare_file(stem + ".moc.cc")
        outputs.append(out_src)

        # Use a shell action to invoke the system moc
        ctx.actions.run_shell(
            inputs = [header],
            outputs = [out_src],
            command = "\n".join([
                "set -e",
                "\"%s\" \"%s\" -o \"%s\"" % (ctx.attr.moc_path, header.path, out_src.path),
            ]),
            progress_message = "Generating MOC for %s" % header.path,
        )

    return [
        DefaultInfo(files = depset(outputs)),
    ]

qt_moc = rule(
    implementation = _qt_moc_impl,
    attrs = {
        "hdrs": attr.label_list(
            allow_files = [".h", ".hpp"],
            doc = "Header files that may contain Q_OBJECT",
        ),
        "moc_path": attr.string(
            default = "/usr/lib/qt6/libexec/moc",
            doc = "Absolute path to the Qt6 moc executable",
        ),
    },
    doc = "Generates C++ sources (.moc.cc) via Qt's moc for the given headers.",
)
