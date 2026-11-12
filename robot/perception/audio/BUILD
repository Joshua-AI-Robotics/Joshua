load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "audio_capture",
    hdrs = ["audio_capture.h"],
    deps = ["//robot/perception/interfaces:audio_interface"],
)

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
    name = "audio_capture_test",
    srcs = [
        "alsa_audio.cc",
        "alsa_audio.h",
        "audio_capture_test.cc",
    ],
    linkopts = [
        "-lasound",
        "-Wl,--wrap=snd_pcm_open",
        "-Wl,--wrap=snd_pcm_close",
        "-Wl,--wrap=snd_pcm_set_params",
        "-Wl,--wrap=snd_pcm_get_params",
        "-Wl,--wrap=snd_pcm_start",
        "-Wl,--wrap=snd_pcm_avail_update",
        "-Wl,--wrap=snd_pcm_readi",
    ],
    deps = [
        ":audio_capture",
        ":audio_config",
        "//robot/perception/interfaces:audio_interface",
        "//robot/perception/proto:perception_cc_proto",
        "@googletest//:gtest_main",
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
        "-Wl,--wrap=snd_pcm_avail_update",
    ],
    deps = [
        ":audio_config",
        "//robot/perception/interfaces:audio_interface",
        "//robot/perception/proto:perception_cc_proto",
        "@googletest//:gtest_main",
    ],
)
