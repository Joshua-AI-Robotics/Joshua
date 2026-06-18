load("@rules_python//python:defs.bzl", "py_library")

def model_requirement(model, name):
    """Resolve a pip requirement from a per-model hub."""
    name = name.replace("-", "_").lower()
    hub = "joshua_pip_" + model
    hub_312 = hub + "_312"
    return select({
        "@rules_python//python/config_settings:is_python_3.10": ["@" + hub + "//" + name],
        "@rules_python//python/config_settings:is_python_3.12": ["@" + hub_312 + "//" + name],
        "//conditions:default": ["@" + hub + "//" + name],
    })
