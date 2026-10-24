def mcp_requirement(name):
    name = name.replace("-", "_").lower()
    return select({
        "@rules_python//python/config_settings:is_python_3.10": ["@joshua_pip_mcp//" + name],
        "@rules_python//python/config_settings:is_python_3.12": ["@joshua_pip_mcp_312//" + name],
        "//conditions:default": ["@joshua_pip_mcp//" + name],
    })
