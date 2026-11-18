filegroup(
    name = "records",
    srcs = glob([
        "**/*.json",
        "evidence/**",
    ]),
    visibility = ["//visibility:public"],
)

filegroup(
    name = "guides",
    srcs = glob(
        ["**/*.md"],
        exclude = ["evidence/*.md"],
    ),
    visibility = ["//visibility:public"],
)

filegroup(
    name = "references",
    srcs = [
        "//:firmware/arduino/AGENTS.md",
        "//config/config_preset:config_presets",
        "//firmware/am243:AGENTS.md",
        "//firmware/esp32:AGENTS.md",
        "//firmware/teensy/41:AGENTS.md",
        "//robot:perception/AGENTS.md",
        "//robot/board/esp32:esp32_board.h",
        "//robot/board/joshua_wire:joshua_wire_board.cc",
        "//robot/board/teensy:teensy_board.h",
        "//robot/perception/camera:cv_camera.cc",
    ],
    visibility = ["//visibility:public"],
)
