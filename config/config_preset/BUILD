filegroup(
    name = "config_presets",
    srcs = glob(["**/*.pbtxt"]),
    visibility = ["//visibility:public"],
)

cc_test(
    name = "config_preset_validation_test",
    srcs = ["config_preset_validation_test.cc"],
    data = [":config_presets"],
    deps = [
        "//config:config_utils",
        "@googletest//:gtest_main",
    ],
)
