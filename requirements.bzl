load("@rules_python//python:defs.bzl", "py_library")

def requirement(name):
    # Normalize name (replace - with _) as per rules_python convention
    name = name.replace("-", "_").lower()
    
    # Return a list containing the selected target
    return select({
        "@rules_python//python/config_settings:is_python_3.10": ["@project_joshua_pip_deps//" + name],
        "@rules_python//python/config_settings:is_python_3.12": ["@project_joshua_pip_deps_312//" + name],
        "//conditions:default": ["@project_joshua_pip_deps//" + name],
    })
