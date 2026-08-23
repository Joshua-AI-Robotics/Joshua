load("@rules_python//python:defs.bzl", "py_binary", "py_library", "py_test")

package(default_visibility = ["//visibility:public"])

# Host-side Pybricks/SPIKE bring-up tooling. Deliberately NOT under robot/:
# the Python robot layer was removed in docs/BOARD_LAYER_RFC.md §10 Phase 9
# and robot/ is C++ only. Joshua has no SPIKE board type any more, so this is
# the only way to drive a hub; nothing on the runtime path depends on it.

py_library(
    name = "pybricks_ble_transport",
    srcs = ["pybricks_ble_transport.py"],
)

py_library(
    name = "pybricks_driver",
    srcs = ["pybricks_driver.py"],
    deps = [
        ":pybricks_ble_transport",
        "//robot/action/proto:action_packet_py_proto",
    ],
)

py_test(
    name = "pybricks_driver_test",
    srcs = ["pybricks_driver_test.py"],
    deps = [
        ":pybricks_driver",
        "//robot/action/proto:action_packet_py_proto",
    ],
)

py_binary(
    name = "pybricks_ble_smoke",
    srcs = ["pybricks_ble_smoke.py"],
    tags = ["manual"],  # Talks to a real hub over BLE and moves a real motor.
    deps = [
        ":pybricks_driver",
        "//robot/action/proto:action_packet_py_proto",
    ],
)
