exports_files(["VERSION"])

platform(
    name = "jetson_orin_nano",
    constraint_values = [
        "@platforms//cpu:arm64",
        "@platforms//os:linux",
    ],
    visibility = ["//visibility:public"],
)

# Platform selectors for x86_64 vs aarch64
config_setting(
    name = "cpu_x86_64",
    constraint_values = ["@platforms//cpu:x86_64"],
    visibility = ["//visibility:public"],
)

config_setting(
    name = "cpu_aarch64",
    constraint_values = ["@platforms//cpu:aarch64"],
    visibility = ["//visibility:public"],
)
