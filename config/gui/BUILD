load("@rules_cc//cc:defs.bzl", "cc_binary")

cc_binary(
    name = "gui",
    srcs = ["main.cc"],
    deps = [
        "@local_deps//:core",
        "@local_deps//:highgui",
        "@local_deps//:imgproc",
    ],
    copts = [
        "-std=c++17",
    ],
)
