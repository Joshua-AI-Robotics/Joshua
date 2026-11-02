load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library", "cc_test")

cc_library(
    name = "node_generator",
    srcs = ["node_generator.cc"],
    hdrs = ["node_generator.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":runtime_observation",
        "//config:config_utils",
        "//config:validation",
        "//ros2/proto:node_cc_proto",
        "//utils:status_macros",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/status:status",
        "@glog",
    ],
)

cc_library(
    name = "runtime_observation",
    srcs = ["runtime_observation.cc"],
    hdrs = ["runtime_observation.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//config/proto:config_cc_proto",
        "//launcher:version",
        "@abseil-cpp//absl/status:statusor",
        "@protobuf",
        "@ros2_deps//:cpp_rcl_interfaces",
        "@ros2_deps//:cpp_sensor_msgs",
        "@ros2_deps//:rclcpp",
    ],
)

cc_binary(
    name = "observation_fixture",
    testonly = True,
    srcs = ["observation_fixture.cc"],
    visibility = ["//tools/mcp:__pkg__"],
    deps = [
        ":runtime_observation",
        "@ros2_deps//:cpp_rcl_interfaces",
        "@ros2_deps//:cpp_sensor_msgs",
        "@ros2_deps//:rclcpp",
    ],
)

cc_test(
    name = "runtime_observation_test",
    srcs = ["runtime_observation_test.cc"],
    deps = [
        ":runtime_observation",
        "@googletest//:gtest_main",
    ],
)
