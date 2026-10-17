def monitor_requirement(name):
    return select({
        "@rules_python//python/config_settings:is_python_3.12": ["@joshua_pip_monitor_312//" + name],
        "//conditions:default": ["@joshua_pip_monitor//" + name],
    })
