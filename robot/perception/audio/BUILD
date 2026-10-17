load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "audio_config",
    hdrs = ["audio_config.h"],
    deps = [
        "//robot/perception/proto:perception_cc_proto",
        "@abseil-cpp//absl/status:status",
    ],
)

cc_library(
    name = "alsa_audio",
    srcs = ["alsa_audio.cc"],
    hdrs = ["alsa_audio.h"],
    linkopts = ["-lasound"],
    deps = [
        ":audio_config",
        "//robot/perception/interfaces:audio_interface",
        "//robot/perception/proto:perception_cc_proto",
    ],
)

cc_test(
    name = "alsa_audio_test",
    # Link the driver directly so the fault-injection wrapper also applies in
    # fully dynamic ARM builds (the final linker cannot wrap calls inside a DSO).
    srcs = [
        "alsa_audio.cc",
        "alsa_audio.h",
        "alsa_audio_test.cc",
    ],
    linkopts = [
        "-lasound",
        "-Wl,--wrap=snd_pcm_readi",
    ],
    deps = [
        ":audio_config",
        "//robot/perception/interfaces:audio_interface",
        "//robot/perception/proto:perception_cc_proto",
        "@googletest//:gtest_main",
    ],
)
