load("@rules_python//python:defs.bzl", "py_library", "py_test")
load("//:requirements.bzl", "requirement")

package(default_visibility = ["//visibility:public"])

py_library(
    name = "types",
    srcs = ["types.py"],
    deps = [
        "//ai/inference/proto:inference_py_proto",
    ] + requirement("protobuf"),
)

py_library(
    name = "adapter",
    srcs = ["adapter.py"],
    deps = [
        ":types",
        "//ai/proto:ai_model_py_proto",
        "//config/proto:config_py_proto",
    ] + requirement("protobuf"),
)

py_library(
    name = "manifest",
    srcs = ["manifest.py"],
    deps = [
        ":adapter",
        "//ai/inference/proto:model_manifest_py_proto",
        "//ai/proto:ai_model_py_proto",
    ] + requirement("protobuf"),
)

py_library(
    name = "environment_manager",
    srcs = ["environment_manager.py"],
    deps = [
        ":manifest",
        "//ai/inference/proto:model_manifest_py_proto",
    ],
)

py_library(
    name = "observation_codec",
    srcs = ["observation_codec.py"],
    deps = [
        ":types",
        "//ros2:image_converter",
        "//ros2/proto:ros2_data_type_py_proto",
    ] + requirement("numpy"),
)

py_library(
    name = "host",
    srcs = ["host.py"],
    deps = [
        ":adapter",
        ":observation_codec",
        ":types",
        "//ai/models:registry",
        "//ai/proto:ai_model_py_proto",
        "//config/proto:config_py_proto",
        "//robot/action/proto:action_py_proto",
        "//ros2:ros2_type_resolver",
        "//ros2/proto:ros2_data_type_py_proto",
        "//ros2/utils:packet_parser_py",
        "//ros2/utils:qos_setting_py",
    ] + requirement("protobuf"),
)

py_test(
    name = "host_startup_test",
    srcs = ["tests/host_startup_test.py"],
    main = "tests/host_startup_test.py",
    deps = [
        ":adapter",
        ":host",
        ":types",
        "//ai/proto:ai_model_py_proto",
        "//config/proto:config_py_proto",
        "//ros2/proto:ros2_data_type_py_proto",
        "//ros2/utils:ros_python_paths_py",
    ],
)

py_library(
    name = "host_main",
    srcs = ["host_main.py"],
    deps = [
        ":host",
        "//ros2:node_runner_py",
        "//ros2/utils:ros_python_paths_py",
    ],
)

py_library(
    name = "inference_launcher",
    srcs = ["inference_launcher.py"],
    data = [
        "//:requirements.lock",
        "//:requirements_3.12.lock",
        "//ai/models:locks",
        "//ai/models:manifests",
        "//ai/models:sources",
    ],
    deps = [
        ":environment_manager",
        ":host_main",
        ":manifest",
        "//ai/inference/proto:model_manifest_py_proto",
        "//config/proto:config_py_proto",
    ] + requirement("protobuf"),
)

py_library(
    name = "motor_policy",
    srcs = ["motor_policy.py"],
    deps = [
        "//ai/inference/proto:motor_policy_py_proto",
        "//robot/action/proto:action_packet_py_proto",
    ],
)

py_test(
    name = "motor_policy_test",
    srcs = ["tests/motor_policy_test.py"],
    data = [
        "testdata/rotary_gripper.pbtxt",
        "//config:joshua_validate_config",
    ],
    main = "tests/motor_policy_test.py",
    deps = [
        ":motor_policy",
        ":types",
        "//ai/inference/proto:motor_policy_py_proto",
        "//ai/models/random_noise:adapter",
        "//config/proto:config_py_proto",
        "//robot/action/proto:action_packet_py_proto",
    ] + requirement("protobuf"),
)
