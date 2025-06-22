load("@rules_cc//cc:defs.bzl", "cc_library")

package(default_visibility = ["//visibility:public"])

# --- Main, Consolidated Target ---
# Your code should depend on this single target: "@opencv//:opencv"
cc_library(
    name = "opencv",
    # This single dependency on :videoio will pull in everything else transitively.
    deps = [":videoio"],
)

# --- Individual Module Definitions ---

# A base library for headers that live directly in opencv2/
# This is what was missing and caused the last error.
cc_library(
    name = "opencv_headers",
    hdrs = glob(["include/opencv4/opencv2/*.hpp"]),
    includes = ["include/opencv4"],
)

cc_library(
    name = "core",
    hdrs = glob(["include/opencv4/opencv2/core/**/*.h*"]),
    includes = ["include/opencv4"],
    linkopts = ["-lopencv_core"],
    # Core depends on the top-level headers.
    deps = [":opencv_headers"],
)

cc_library(
    name = "highgui",
    hdrs = glob(["include/opencv4/opencv2/highgui/**/*.h*"]),
    includes = ["include/opencv4"],
    linkopts = ["-lopencv_highgui"],
    deps = [":core"],
)

cc_library(
    name = "imgproc",
    hdrs = glob(["include/opencv4/opencv2/imgproc/**/*.h*"]),
    includes = ["include/opencv4"],
    linkopts = ["-lopencv_imgproc"],
    deps = [":core"],
)

cc_library(
    name = "videoio",
    hdrs = glob(["include/opencv4/opencv2/videoio/**/*.h*"]),
    includes = ["include/opencv4"],
    linkopts = ["-lopencv_videoio"],
    # The dependency chain: videoio -> highgui -> core -> opencv_headers
    deps = [":highgui", ":imgproc"],
)