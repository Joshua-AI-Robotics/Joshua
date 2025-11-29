load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "xbox_controller",
    srcs = ["xbox_controller/xbox_controller.cc"],
    hdrs = ["xbox_controller/xbox_controller.h"],
    visibility = ["//visibility:public"],
    deps = [
        "@glog",
        "@local_deps//:libevdev",
    ],
)

cc_library(
    name = "status_macros",
    hdrs = ["status_macros.h"],
    visibility = ["//visibility:public"],
    deps = [
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
    ],
)
