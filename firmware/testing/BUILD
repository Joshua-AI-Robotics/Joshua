load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "arduino",
    testonly = True,
    hdrs = [
        "Arduino.h",
        "Entropy.h",
        "esp_system.h",
    ],
    includes = ["."],
    visibility = ["//firmware:__subpackages__"],
)

exports_files(
    ["serial_firmware_test.cc"],
    visibility = ["//firmware:__subpackages__"],
)

cc_library(
    name = "ti_uart",
    testonly = True,
    hdrs = [
        "ti_sdk/drivers/uart.h",
        "ti_sdk/kernel/dpl/ClockP.h",
    ],
    includes = ["ti_sdk"],
    visibility = ["//firmware/am243/joshua_dual_transport:__pkg__"],
)

cc_library(
    name = "ti_ethercat",
    testonly = True,
    hdrs = [
        "ti_sdk/ecSlvApi.h",
        "ti_sdk/ecSlvApiDef.h",
        "ti_sdk/kernel/dpl/ClockP.h",
        "ti_sdk/kernel/dpl/HwiP.h",
        "ti_sdk/kernel/dpl/TaskP.h",
        "ti_sdk/security/crypto/pka/hw_include/cslr_cp_ace.h",
        "ti_sdk/security/crypto/rng/rng.h",
        "ti_sdk/security/crypto/sa2ul/sa2ul.h",
    ],
    includes = ["ti_sdk"],
    visibility = ["//firmware/am243/joshua_dual_transport:__pkg__"],
)
