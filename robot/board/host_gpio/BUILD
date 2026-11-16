load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "gpio_backend",
    hdrs = ["gpio_backend.h"],
    deps = [
        "//robot/board/pin:pin_resource",
        "@abseil-cpp//absl/status:status",
        "@abseil-cpp//absl/status:statusor",
    ],
)

cc_library(
    name = "linux_gpio_backend",
    srcs = ["linux_gpio_backend.cc"],
    hdrs = ["linux_gpio_backend.h"],
    deps = [
        ":gpio_backend",
        "@abseil-cpp//absl/strings",
    ],
)

cc_library(
    name = "host_gpio_board",
    srcs = ["host_gpio_board.cc"],
    hdrs = ["host_gpio_board.h"],
    deps = [
        ":gpio_backend",
        ":linux_gpio_backend",
        "//robot/board/interfaces:board_interfaces",
        "//utils:status_macros",
        "@abseil-cpp//absl/strings",
    ],
)

cc_test(
    name = "host_gpio_board_test",
    srcs = ["host_gpio_board_test.cc"],
    deps = [
        ":host_gpio_board",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "linux_gpio_backend_test",
    srcs = ["linux_gpio_backend_test.cc"],
    deps = [
        ":linux_gpio_backend",
        "@googletest//:gtest_main",
    ],
)
