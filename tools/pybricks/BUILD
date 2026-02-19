load("@rules_python//python:defs.bzl", "py_binary")

package(default_visibility = ["//visibility:public"])

py_binary(
    name = "pybricks_ble_smoke",
    srcs = ["pybricks_ble_smoke.py"],
    deps = [
        "//robot/action/motors/drivers:pybricks_driver_py",
        "//robot/action/proto:action_packet_py_proto",
        "//robot/action/proto:action_py_proto",
        "//robot/comm/proto:comm_py_proto",
    ],
)
