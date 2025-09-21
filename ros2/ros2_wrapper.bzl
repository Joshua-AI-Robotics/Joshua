"""Custom Bazel rule to generate wrapper scripts for ROS2 binaries."""

load("@com_github_mvukov_rules_ros2//ros2:cc_defs.bzl", "ros2_cpp_binary")
load("@com_github_mvukov_rules_ros2//ros2:py_defs.bzl", "ros2_py_binary")

def _ros2_wrapper_script_impl(ctx):
    """Implementation of the ros2_wrapper_script rule."""
    binary_name = ctx.attr.binary_name
    is_python = ctx.attr.is_python
    script = ctx.actions.declare_file(binary_name + "_wrapper.sh")

    if is_python:
        # For Python, cd into runfiles and execute the generated launcher inside runfiles
        script_content = """#!/bin/bash

# Auto-generated wrapper script for {binary_name} (python)
set -e

SCRIPT_DIR="$(cd "$(dirname "${{BASH_SOURCE[0]}}")" && pwd)"

# Change to the runfiles directory where the relative paths will work
cd "${{SCRIPT_DIR}}/{binary_name}.runfiles/_main"

# Execute the generated launcher within runfiles so it can locate _impl and ament setup
exec "./ros2/{binary_name}_launch" "$@"
""".format(binary_name = binary_name)
    else:
        # For C++, cd into runfiles and execute the generated launcher within runfiles
        script_content = """#!/bin/bash

# Auto-generated wrapper script for {binary_name}
set -e

# Get the directory where this script is located
SCRIPT_DIR="$(cd "$(dirname "${{BASH_SOURCE[0]}}")" && pwd)"

# Change to the runfiles directory where the relative paths will work
cd "${{SCRIPT_DIR}}/{binary_name}.runfiles/_main"

# Execute the launcher which configures AMENT and locates the _impl binary
exec "./ros2/{binary_name}_launch" "$@"
""".format(binary_name = binary_name)

    ctx.actions.write(
        output = script,
        content = script_content,
        is_executable = True,
    )
    
    return [DefaultInfo(files = depset([script]))]

ros2_wrapper_script = rule(
    implementation = _ros2_wrapper_script_impl,
    attrs = {
        "binary_name": attr.string(mandatory = True),
        "is_python": attr.bool(default = False),
    },
)

def ros2_cpp_binary_with_wrapper(name, **kwargs):
    """ros2_cpp_binary that also generates a wrapper script in one go."""
    
    # Force set_up_ament = True to ensure runfiles are created
    kwargs["set_up_ament"] = True
    
    # Generate the wrapper script first
    wrapper_name = name + "_wrapper"
    ros2_wrapper_script(
        name = wrapper_name,
        binary_name = name,
        is_python = False,
    )
    
    # Add the wrapper script to the data of the main binary
    existing_data = kwargs.get("data", [])
    kwargs["data"] = existing_data + [wrapper_name]
    
    # Call the original ros2_cpp_binary rule
    ros2_cpp_binary(name = name, **kwargs)

def ros2_py_binary_with_wrapper(name, **kwargs):
    """ros2_py_binary that also generates a wrapper script in one go."""

    # Ensure ament setup and runfiles are created for the launcher
    kwargs["set_up_ament"] = True

    # Generate the wrapper script first
    wrapper_name = name + "_wrapper"
    ros2_wrapper_script(
        name = wrapper_name,
        binary_name = name,
        is_python = True,
    )

    # Add the wrapper script to the data of the main binary
    existing_data = kwargs.get("data", [])
    kwargs["data"] = existing_data + [wrapper_name]

    # Call the original ros2_py_binary rule
    ros2_py_binary(name = name, **kwargs) 