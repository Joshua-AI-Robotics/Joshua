load("@rules_python//python:defs.bzl", "py_library")
load("@project_joshua_pip_deps//:requirements.bzl", "requirement")

package(default_visibility = ["//visibility:public"])

# Main library that combines config and modeling.
# Other packages should depend on this target.
py_library(
    name = "decision_transformer",
    deps = [
        ":decision_transformer_config",
        ":decision_transformer_modeling",
    ],
)

# The model implementation, which now also contains the policy logic.
py_library(
    name = "decision_transformer_modeling",
    srcs = ["modeling_decision_transformer.py"],
    deps = [
        ":decision_transformer_config",
        "//robot/nexus/proto:nexus_packet_py_proto",
        requirement("torch"),
        requirement("transformers"),
    ],
)

# The configuration for our custom model.
py_library(
    name = "decision_transformer_config",
    srcs = ["config_decision_transformer.py"],
    deps = [
        "//config/proto:config_py_proto",
        requirement("transformers"),
    ],
) 