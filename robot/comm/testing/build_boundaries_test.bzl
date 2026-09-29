"""Analysis-time regression guard for the public comm header boundary."""

def _transport_boundary_test_impl(ctx):
    # Private implementations must still link, but their headers must not be
    # propagated through the public factory/capability compilation contexts.
    for library in ctx.attr.libraries:
        for header in library[CcInfo].compilation_context.headers.to_list():
            path = header.short_path
            if ("robot/comm/serial/" in path or
                "robot/comm/ethercat/" in path or
                "/include/soem/" in path):
                fail("%s exposes private implementation header %s" % (library.label, path))
    executable = ctx.actions.declare_file(ctx.label.name + ".sh")
    ctx.actions.write(executable, "#!/bin/sh\nexit 0\n", is_executable = True)
    return [DefaultInfo(executable = executable)]

transport_boundary_test = rule(
    implementation = _transport_boundary_test_impl,
    attrs = {"libraries": attr.label_list(providers = [CcInfo])},
    test = True,
)
