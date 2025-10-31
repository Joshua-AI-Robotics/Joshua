load("@com_github_mvukov_rules_ros2//ros2:cc_defs.bzl", "ros2_cpp_binary")
load("@com_github_mvukov_rules_ros2//ros2:py_defs.bzl", "ros2_py_binary")
load("@project_joshua_pip_deps//:requirements.bzl", "requirement")
load("@rules_python//python:defs.bzl", "py_library")

package(default_visibility = ["//visibility:public"])

# Central list of ROS2 node binary targets
ROS2_NODE_TARGETS = [
    ":camera_publisher",
    ":encoder_publisher",
    ":actuator_subscriber",
    ":mock_inference",
    ":mock_inference_py",
    ":operational_limit_calibration",
]

cc_library(
    name = "node_runner",
    hdrs = ["node_runner.h"],
    data = [
        "//config/config_preset:config_presets",
    ],
    deps = [
        "//config:config_utils",
        "//config/proto:config_cc_proto",
        "@ros2_common_interfaces//:cpp_std_msgs",
        "@ros2_rclcpp//:rclcpp",
    ],
)

py_library(
    name = "node_runner_py",
    srcs = [
        "node_runner.py",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "//config/proto:config_py_proto",
        "@ros2_rclpy//:rclpy",
        requirement("protobuf"),
    ],
    visibility = ["//visibility:public"],
)

py_library(
    name = "inference_base",
    srcs = [
        "inference_base.py",
    ],
    deps = [
        "@ros2_rclpy//:rclpy",
        "@ros2_common_interfaces//:py_std_msgs",
        "@ros2_common_interfaces//:py_sensor_msgs",
        "//config/proto:config_py_proto",
    ],
    visibility = ["//visibility:public"],
)

ros2_cpp_binary(
    name = "camera_publisher",
    srcs = ["camera_publisher.cc"],
    data = [
        "//config/config_preset:config_presets",
    ],
    set_up_ament = True,
    deps = [
        "//config/proto:config_cc_proto",
        "//robot/perception/factory:perception_factory",
        "//robot/perception/proto:perception_packet_cc_proto",
        "//ros2:node_runner",
        "@ros2_common_interfaces//:cpp_sensor_msgs",
        "@ros2_common_interfaces//:cpp_std_msgs",
        "@ros2_rclcpp//:rclcpp",
    ],
)

ros2_cpp_binary(
    name = "encoder_publisher",
    srcs = ["encoder_publisher.cc"],
    data = [
        "//config/config_preset:config_presets",
    ],
    set_up_ament = True,
    deps = [
        "//config/proto:config_cc_proto",
        "//robot/perception/factory:perception_factory",
        "//robot/perception/proto:perception_cc_proto",
        "//robot/perception/proto:perception_packet_cc_proto",
        "//ros2:node_runner",
        "@ros2_common_interfaces//:cpp_std_msgs",
        "@ros2_rclcpp//:rclcpp",
    ],
)

ros2_cpp_binary(
    name = "lidar_publisher",
    srcs = ["lidar_publisher.cc"],
    data = [
        "//config/config_preset:config_presets",
    ],
    deps = [
        "@ros2_rclcpp//:rclcpp",
        "@ros2_common_interfaces//:cpp_std_msgs",
        "@ros2_common_interfaces//:cpp_sensor_msgs",
        "//robot/perception/factory:perception_factory",
        "//config/proto:config_cc_proto",
        "//robot/perception/proto:perception_packet_cc_proto",
        "//robot/perception/proto:perception_cc_proto",
        "//ros2:node_runner",
    ],
    set_up_ament = True,
)

ros2_cpp_binary(
    name = "actuator_subscriber",
    srcs = ["actuator_subscriber.cc"],
    data = [
        "//config/config_preset:config_presets",
    ],
    set_up_ament = True,
    deps = [
        "//config/proto:config_cc_proto",
        "//robot/action/factory:action_factory",
        "//robot/action/proto:action_cc_proto",
        "//robot/action/proto:action_packet_cc_proto",
        "//ros2:node_runner",
        "@ros2_common_interfaces//:cpp_std_msgs",
        "@ros2_common_interfaces//:cpp_std_srvs",
        "@ros2_rclcpp//:rclcpp",
    ],
)

ros2_cpp_binary(
    name = "mock_inference",
    srcs = ["mock_inference.cc"],
    data = [
        "//config/config_preset:config_presets",
    ],
    set_up_ament = True,
    deps = [
        "//config/proto:ai_cc_proto",
        "//config/proto:config_cc_proto",
        "//config/proto:robot_cc_proto",
        "//ros2:node_runner",
        "@ros2_common_interfaces//:cpp_sensor_msgs",
        "@ros2_common_interfaces//:cpp_std_msgs",
        "@ros2_rclcpp//:rclcpp",
    ],
)

ros2_py_binary(
    name = "mock_inference_py",
    srcs = [
        "mock_inference.py",
    ],
    data = [
        "//config/config_preset:config_presets",
    ],
    main = "mock_inference.py",
    set_up_ament = True,
    deps = [
        "//config/proto:config_py_proto",
        "//ros2:node_runner_py",
    ],
    data = [
        "//config/config_preset:config_presets",
    ],
    set_up_ament = True,
)

ros2_cpp_binary(
    name = "operational_limit_calibration",
    srcs = ["operational_limit_calibration.cc"],
    data = [
        "//config/config_preset:config_presets",
    ],
    set_up_ament = True,
    deps = [
        "//config/proto:config_cc_proto",
        "//ros2:node_runner",
        "@ros2_common_interfaces//:cpp_std_msgs",
        "@ros2_rclcpp//:rclcpp",
    ],
)

genrule(
    name = "ros2_node_wrapper",
    outs = [
        "ros2_node_wrapper.sh",
    ],
    cmd = """
cat > $(location ros2_node_wrapper.sh) << 'EOF'
#!/usr/bin/env bash
set -euo pipefail

if [ $$# -lt 1 ]; then
  echo "Usage: $$0 <node_name> [args...]" >&2
  exit 1
fi

NODE="$$1"; shift || true
SCRIPT_DIR="$$(cd "$$(dirname "$$0")" && pwd)"

# Case 1a: Impl sits next to this script
if [ -x "$$SCRIPT_DIR/$${NODE}_impl" ]; then
  cd "$$SCRIPT_DIR"
  # Export runfiles env vars to help ROS 2 locate resources
  WS_DIR="$$(dirname "$$SCRIPT_DIR")"
  export RUNFILES_DIR="$$WS_DIR"
  export TEST_SRCDIR="$$WS_DIR"
  unset RUNFILES_MANIFEST_FILE || true
  unset JAVA_RUNFILES || true
  exec "./$${NODE}_impl" "$$@"
fi

# Case 1b: Impl sits under ros2/ while this script is at top-level
if [ -x "$$SCRIPT_DIR/ros2/$${NODE}_impl" ]; then
  cd "$$SCRIPT_DIR/ros2"
  WS_DIR="$$(dirname "$$SCRIPT_DIR")"
  export RUNFILES_DIR="$$WS_DIR"
  export TEST_SRCDIR="$$WS_DIR"
  unset RUNFILES_MANIFEST_FILE || true
  unset JAVA_RUNFILES || true
  exec "./$${NODE}_impl" "$$@"
fi

# Case 2: Runfiles layout. Search any *.runfiles under top-level and ros2/ subdir.
for CAND in "$$SCRIPT_DIR" "$$SCRIPT_DIR/ros2"; do
  for RUNFILES_DIR in "$$CAND"/*.runfiles; do
    if [ -d "$$RUNFILES_DIR/_main/ros2" ]; then
      cd "$$RUNFILES_DIR/_main/ros2"
      WS_DIR="$$(dirname "$$PWD")"
      export RUNFILES_DIR="$$WS_DIR"
      export TEST_SRCDIR="$$WS_DIR"
      unset RUNFILES_MANIFEST_FILE || true
      unset JAVA_RUNFILES || true
      exec "./$${NODE}_impl" "$$@"
    fi
    if [ -d "$$RUNFILES_DIR/__main__/ros2" ]; then
      cd "$$RUNFILES_DIR/__main__/ros2"
      WS_DIR="$$(dirname "$$PWD")"
      export RUNFILES_DIR="$$WS_DIR"
      export TEST_SRCDIR="$$WS_DIR"
      unset RUNFILES_MANIFEST_FILE || true
      unset JAVA_RUNFILES || true
      exec "./$${NODE}_impl" "$$@"
    fi
  done
done

echo "Could not locate '$${NODE}_impl' relative to '$$SCRIPT_DIR'." >&2
exit 1
EOF
chmod +x $(location ros2_node_wrapper.sh)
""",
    visibility = ["//visibility:public"],
)
