load("@rules_cc//cc:defs.bzl", "cc_library")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "board_interfaces",
    hdrs = [
        "board_channel.h",
        "board_interface.h",
        "digital_pin.h",
        "scalar_sensor.h",
    ],
    deps = [
        "//robot/board/pin:pin_resource",
        "//robot/board/proto:board_cc_proto",
        "//utils:robot_time",
        "@abseil-cpp//absl/status:status",
        "@abseil-cpp//absl/status:statusor",
    ],
)
