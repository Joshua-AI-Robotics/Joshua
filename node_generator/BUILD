load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library", "cc_test")

cc_binary(
    name = "process_fixture",
    testonly = True,
    srcs = ["process_fixture.cc"],
    deps = ["//utils:joshua_runtime"],
)

cc_test(
    name = "process_supervisor_test",
    srcs = ["process_supervisor_test.cc"],
    data = [":process_fixture"],
    deps = [
        ":process_supervisor",
        "//utils:joshua_runtime",
        "//utils:scope_exit",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "process_supervisor",
    srcs = ["process_supervisor.cc"],
    hdrs = ["process_supervisor.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//utils:joshua_runtime",
        "@abseil-cpp//absl/status:status",
    ],
)

cc_library(
    name = "launch_spec",
    srcs = ["launch_spec.cc"],
    hdrs = ["launch_spec.h"],
    deps = [
        ":process_supervisor",
        "//ros2/proto:node_cc_proto",
        "@abseil-cpp//absl/status:statusor",
        "@glog",
    ],
)

cc_library(
    name = "node_generator",
    srcs = ["node_generator.cc"],
    hdrs = ["node_generator.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":launch_spec",
        ":process_supervisor",
        "//config:config_utils",
        "//config:validation",
        "//config/proto:config_cc_proto",
        "//ros2/proto:node_cc_proto",
        "//utils:status_macros",
        "//utils:joshua_runtime",
        "@abseil-cpp//absl/status:status",
        "@glog",
    ],
)
