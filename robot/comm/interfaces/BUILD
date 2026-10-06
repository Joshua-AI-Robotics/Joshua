load("@rules_cc//cc:defs.bzl", "cc_library")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "byte_stream",
    hdrs = ["byte_stream.h"],
    deps = [
        "@abseil-cpp//absl/status:status",
        "@abseil-cpp//absl/status:statusor",
    ],
)

cc_library(
    name = "message_transport",
    hdrs = ["message_transport.h"],
    deps = [
        "@abseil-cpp//absl/status:status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_library(
    name = "legacy_message_transport",
    hdrs = ["legacy_message_transport.h"],
    deps = [":message_transport"],
)

cc_library(
    name = "correlated_cyclic_transport",
    hdrs = ["correlated_cyclic_transport.h"],
    deps = [
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/time",
        "@abseil-cpp//absl/types:span",
    ],
)
