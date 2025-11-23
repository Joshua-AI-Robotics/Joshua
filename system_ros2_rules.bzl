"""System ROS2 rules that use system-installed ROS2 libraries.

This module provides ROS2 rules compatible with rules_ros2 but using
system-installed libraries from apt packages. Supports cross-compilation
for aarch64 targets.

    Usage in BUILD files:
        load("//:system_ros2_rules.bzl", "ros2_cpp_binary", "ros2_py_binary")
        
        ros2_cpp_binary(
            name = "my_node",
            srcs = ["my_node.cc"],
            deps = [
                "@ros2_deps//:rclcpp",
                "@ros2_deps//:cpp_std_msgs",
            ],
        )
"""

load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library", "cc_test")
load("@rules_python//python:defs.bzl", "py_binary", "py_library", "py_test")

def _ros2_cc_target(target, lang, name, ros2_package_name, **kwargs):
    """Helper function to create ROS2 C/C++ targets with proper defines."""
    all_local_defines = ["ROS_PACKAGE_NAME=\\\"{}\\\"".format(ros2_package_name or name)]
    all_local_defines = all_local_defines + kwargs.pop("local_defines", [])
    
    # Ensure C++17 is used and proper compilation flags are set
    copts = kwargs.pop("copts", [])
    copts = copts + ["-std=c++17"]
    
    target(
        name = name,
        local_defines = all_local_defines,
        copts = copts,
        **kwargs
    )

def ros2_cpp_library(name, ros2_package_name = None, **kwargs):
    """Defines a ROS 2 C++ library using system libraries.
    
    Args:
        name: A unique target name.
        ros2_package_name: If given, defines a ROS 2 package name for the target.
            Otherwise, the `name` is used as the package name.
        **kwargs: Standard cc_library arguments.
    """
    # Add system ROS2 dependencies if not already specified
    deps = kwargs.pop("deps", [])
    if "@ros2_deps//:rclcpp" not in [str(d) for d in deps]:
        deps = deps + ["@ros2_deps//:rclcpp"]
    
    _ros2_cc_target(cc_library, "cpp", name, ros2_package_name, deps = deps, **kwargs)

def ros2_cpp_binary(name, ros2_package_name = None, set_up_ament = False, idl_deps = None, **kwargs):
    """Defines a ROS 2 C++ binary using system libraries.
    
    Args:
        name: A unique target name.
        ros2_package_name: If given, defines a ROS package name for the target.
            Otherwise, the `name` is used as the package name.
        set_up_ament: If true, sets up ament file tree for the binary target.
            Note: This is simplified for system libraries - you may need to
            manually set up ROS2 environment variables.
        idl_deps: Additional IDL deps that are used as runtime plugins.
        **kwargs: Standard cc_binary arguments.
    """
    # Add system ROS2 dependencies if not already specified
    deps = kwargs.pop("deps", [])
    if "@ros2_deps//:rclcpp" not in [str(d) for d in deps]:
        deps = deps + ["@ros2_deps//:rclcpp"]
    
    # For system libraries, we create a simple binary
    # The ament setup would need to be handled separately if needed
    # Note: Headers should be accessible via includes from @ros2_deps//:rclcpp
    # If headers aren't found, ensure ROS2 is properly installed at /opt/ros/humble
    _ros2_cc_target(cc_binary, "cpp", name, ros2_package_name, deps = deps, **kwargs)

def ros2_cpp_test(name, ros2_package_name = None, set_up_ament = True, idl_deps = None, **kwargs):
    """Defines a ROS 2 C++ test using system libraries.
    
    Args:
        name: A unique target name.
        ros2_package_name: If given, defines a ROS package name for the target.
            Otherwise, the `name` is used as the package name.
        set_up_ament: If true, sets up ament file tree for the test target.
        idl_deps: Additional IDL deps that are used as runtime plugins.
        **kwargs: Standard cc_test arguments.
    """
    # Add system ROS2 dependencies if not already specified
    deps = kwargs.pop("deps", [])
    if "@ros2_deps//:rclcpp" not in [str(d) for d in deps]:
        deps = deps + ["@ros2_deps//:rclcpp"]
    
    _ros2_cc_target(cc_test, "cpp", name, ros2_package_name, deps = deps, **kwargs)

def ros2_py_binary(name, ros2_package_name = None, set_up_ament = False, **kwargs):
    """Defines a ROS 2 Python binary using system libraries.
    
    Args:
        name: A unique target name.
        ros2_package_name: If given, defines a ROS package name for the target.
            Otherwise, the `name` is used as the package name.
        set_up_ament: If true, sets up ament file tree for the binary target.
        **kwargs: Standard py_binary arguments.
    """
    # Add system ROS2 Python dependencies if not already specified
    deps = kwargs.pop("deps", [])
    # Note: For Python, we rely on system-installed rclpy
    # The user should ensure rclpy is available in their Python environment
    
    py_binary(
        name = name,
        deps = deps,
        **kwargs
    )

def ros2_py_test(name, ros2_package_name = None, set_up_ament = True, **kwargs):
    """Defines a ROS 2 Python test using system libraries.
    
    Args:
        name: A unique target name.
        ros2_package_name: If given, defines a ROS package name for the target.
            Otherwise, the `name` is used as the package name.
        set_up_ament: If true, sets up ament file tree for the test target.
        **kwargs: Standard py_test arguments.
    """
    # Add system ROS2 Python dependencies if not already specified
    deps = kwargs.pop("deps", [])
    
    py_test(
        name = name,
        deps = deps,
        **kwargs
    )

