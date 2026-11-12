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
        ":host_peripherals",
        ":linux_gpio_backend",
        ":resource_leases",
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

cc_library(
    name = "peripheral_backend",
    srcs = ["peripheral_backend.cc"],
    hdrs = ["peripheral_backend.h"],
    deps = [
        "//robot/board/interfaces:board_interfaces",
        "//robot/board/pin:peripheral_resource",
        "//robot/comm/factory:comm_factory",
        "//utils:status_macros",
    ],
)

cc_library(
    name = "host_peripherals",
    srcs = ["host_peripherals.cc"],
    hdrs = ["host_peripherals.h"],
    deps = [
        ":peripheral_backend",
        "//utils:status_macros",
    ],
)

cc_library(
    name = "resource_leases",
    srcs = ["resource_leases.cc"],
    hdrs = ["resource_leases.h"],
    deps = ["@abseil-cpp//absl/status:statusor"],
)

cc_test(
    name = "peripheral_backend_test",
    srcs = ["peripheral_backend_test.cc"],
    deps = [
        ":peripheral_backend",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "resource_leases_test",
    srcs = ["resource_leases_test.cc"],
    deps = [
        ":resource_leases",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "host_peripherals_test",
    srcs = ["host_peripherals_test.cc"],
    deps = [
        ":host_peripherals",
        "@googletest//:gtest_main",
    ],
)
