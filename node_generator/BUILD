load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library")
load("@rules_pkg//:pkg.bzl", "pkg_tar")

cc_library(
    name = "node_generator",
    srcs = ["node_generator.cc"],
    hdrs = ["node_generator.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//config:config_utils",
        "@abseil-cpp//absl/status:status",
        "@glog",
    ],
)

cc_binary(
    name = "joshua_main",
    srcs = ["joshua_main.cc"],
    data = [
        "//config/config_preset:config_presets",
        "//ros2:actuator_subscriber",
        "//ros2:camera_publisher",
        "//ros2:encoder_publisher",
        "//ros2:mock_inference",
        "//ros2:mock_inference_py",
        "//ros2:operational_limit_calibration",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":node_generator",
        "@gflags",
        "@glog",
    ],
)

# Create a script to fix symlinks after extraction
genrule(
    name = "fix_symlinks_script",
    outs = ["fix_symlinks.sh"],
    cmd = """
cat > $(location fix_symlinks.sh) << 'EOF'
#!/bin/bash
# Fix symlinks for ROS 2 middleware libraries for any present runfiles
shopt -s nullglob
for rf in *.runfiles; do
  if [ -d "$$rf/_main" ]; then
    ln -sf "../com_github_mvukov_rules_ros2++non_module_deps+ros2_rmw_cyclonedds" "$$rf/_main/com_github_mvukov_rules_ros2++non_module_deps+ros2_rmw_cyclonedds" 2>/dev/null || true
  fi
done
EOF
chmod +x $(location fix_symlinks.sh)
""",
)

# Package the native binary and runtime libs into a tar archive
pkg_tar(
    name = "joshua_main_pkg",
    srcs = [
        ":fix_symlinks_script",
        ":joshua_main",
        "//config/config_preset:config_presets",
        "//ros2:ros2_node_wrapper.sh",
    ],
    extension = "tar.gz",
    include_runfiles = True,
    mode = "0755",
    package_dir = "joshua_main",
    visibility = ["//visibility:public"],
)
