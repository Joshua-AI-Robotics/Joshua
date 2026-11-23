load("@rules_python//python:defs.bzl", "py_library", "py_test")
load("//:requirements.bzl", "requirement")
load("//ai/models:requirements.bzl", "model_requirement")

py_library(
    name = "server",
    srcs = ["server.py"],
    deps = ["//ai/models/decisions:choice"] + model_requirement("decisions", "openai") + requirement("aiohttp"),
)

py_test(
    name = "server_test",
    srcs = ["tests/server_test.py"],
    main = "tests/server_test.py",
    deps = [":server"] + model_requirement("decisions", "openai") + requirement("aiohttp"),
)
