load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")
load("@rules_python//python:defs.bzl", "py_binary", "py_library", "py_test")
load("//:requirements.bzl", "requirement")
load("//:system_ros2_rules.bzl", "ros2_cpp_binary")
load(":requirements.bzl", "mcp_requirement")

exports_files([
    "requirements.in",
    "requirements.lock",
    "requirements_3.12.lock",
])

py_library(
    name = "config_validation_lib",
    srcs = [
        "config_validation_server.py",
        "hardware_catalog.py",
        "image_source.py",
        "inspection_adapters.py",
        "inspection_server.py",
        "interfaces.py",
        "snapshot_observer.py",
    ],
    data = [
        "//docs:hardware_catalog.json",
        "//docs:hardware_test_records.json",
    ],
    deps = [":observation_source"] + mcp_requirement("mcp"),
)

py_binary(
    name = "config_validation_server",
    srcs = ["main.py"],
    main = "main.py",
    deps = [":config_validation_lib"],
)

py_test(
    name = "hardware_catalog_test",
    srcs = ["hardware_catalog_test.py"],
    data = [
        "//config/config_preset:config_presets",
        "//docs:SUPPORTED_HARDWARE.md",
        "//docs:TESTED_HARDWARE.md",
        "//docs:hardware_tests/sunplus_spca2281_1080p_2026-11-04.md",
        "//docs:hardware_tests/sunplus_spca2281_2026-11-04.md",
        "//robot/perception/camera:cv_camera.cc",
    ],
    deps = [":config_validation_lib"],
)

py_test(
    name = "config_validation_server_test",
    srcs = ["config_validation_server_test.py"],
    data = [
        ":config_validation_server",
        "//config:joshua_validate_config",
    ],
    deps = [":config_validation_lib"] + mcp_requirement("mcp"),
)

py_test(
    name = "inspection_server_test",
    size = "medium",
    timeout = "moderate",
    srcs = ["inspection_server_test.py"],
    data = [
        ":config_validation_server",
        "//config:joshua_inspect_config",
        "//config:joshua_validate_config",
        "//node_generator:observation_fixture",
    ],
    tags = ["local"],
    deps = [":config_validation_lib"] + mcp_requirement("mcp"),
)

py_library(
    name = "observation_source",
    srcs = ["observation_source.py"],
)

py_test(
    name = "observation_source_test",
    srcs = ["observation_source_test.py"],
    deps = [":observation_source"],
)

py_test(
    name = "image_source_test",
    size = "medium",
    timeout = "moderate",
    srcs = ["image_source_test.py"],
    data = [
        ":config_validation_server",
        ":image_observer",
        ":image_observer_codec_failure_fixture",
        ":image_observer_codec_oversize_fixture",
        ":image_observer_fixture",
        ":image_observer_race_fixture",
        "//config:joshua_inspect_config",
        "//config:joshua_validate_config",
    ],
    tags = ["local"],
    deps = [
        ":config_validation_lib",
        ":observation_source",
    ] + mcp_requirement("mcp") + requirement("pillow"),
)

py_test(
    name = "image_source_lifecycle_test",
    size = "medium",
    srcs = ["image_source_lifecycle_test.py"],
    deps = [
        ":config_validation_lib",
        ":observation_source",
    ],
)

py_binary(
    name = "setup_smoke",
    srcs = ["setup_smoke.py"],
    deps = mcp_requirement("mcp"),
)

py_test(
    name = "setup_smoke_test",
    size = "medium",
    srcs = ["setup_smoke.py"],
    args = ["--test"],
    data = [
        ":config_validation_server",
        "//config:joshua_inspect_config",
        "//config:joshua_validate_config",
    ],
    main = "setup_smoke.py",
    deps = mcp_requirement("mcp"),
)

cc_library(
    name = "ros_source_binding",
    srcs = ["ros_source_binding.cc"],
    hdrs = ["ros_source_binding.h"],
    deps = [
        "//config:config_validation_cli",
        "//config/proto:config_cc_proto",
        "@abseil-cpp//absl/status:status",
        "@abseil-cpp//absl/status:statusor",
        "@protobuf",
    ],
)

cc_test(
    name = "ros_source_binding_test",
    srcs = ["ros_source_binding_test.cc"],
    deps = [
        ":ros_source_binding",
        "//config/proto:config_cc_proto",
        "@googletest//:gtest_main",
        "@protobuf",
    ],
)

ros2_cpp_binary(
    name = "image_observer",
    srcs = ["image_observer.cc"],
    deps = [":image_observer_backend"],
)

cc_library(
    name = "image_observer_backend",
    srcs = ["image_observer_backend.cc"],
    hdrs = ["image_observer_backend.h"],
    deps = [
        ":ros_source_binding",
        "@local_deps//:core",
        "@local_deps//:imgcodecs",
        "@local_deps//:imgproc",
        "@protobuf",
        "@ros2_deps//:cpp_sensor_msgs",
        "@ros2_deps//:rclcpp",
    ],
)

ros2_cpp_binary(
    name = "image_observer_fixture",
    testonly = True,
    srcs = ["image_observer_fixture.cc"],
    deps = [
        "//config/proto:config_cc_proto",
        "@protobuf",
        "@ros2_deps//:cpp_sensor_msgs",
    ],
)

ros2_cpp_binary(
    name = "image_observer_race_fixture",
    testonly = True,
    srcs = ["image_observer_race_fixture.cc"],
    deps = [
        ":image_observer_backend",
        "@local_deps//:imgcodecs",
        "@protobuf",
    ],
)

ros2_cpp_binary(
    name = "image_observer_codec_failure_fixture",
    testonly = True,
    srcs = ["image_observer_race_fixture.cc"],
    defines = ["JOSHUA_TEST_CODEC_FAILURE"],
    deps = [
        ":image_observer_backend",
        "@local_deps//:imgcodecs",
        "@protobuf",
    ],
)

ros2_cpp_binary(
    name = "image_observer_codec_oversize_fixture",
    testonly = True,
    srcs = ["image_observer_race_fixture.cc"],
    defines = ["JOSHUA_TEST_CODEC_OVERSIZE"],
    deps = [
        ":image_observer_backend",
        "@local_deps//:imgcodecs",
        "@protobuf",
    ],
)
