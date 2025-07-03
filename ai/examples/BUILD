load("@rules_python//python:defs.bzl", "py_binary")
load("@project_joshua_pip_deps//:requirements.bzl", "requirement")

py_binary(
    name = "vision_transformer",
    srcs = ["vision_transformer.py"],
    deps = [
        requirement("torch"),
        requirement("transformers"),
        requirement("opencv-python"),
        requirement("Pillow"),
    ],
)
