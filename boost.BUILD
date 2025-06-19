load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "boost_system",
    hdrs = glob([
        "include/boost/system/**/*.hpp",
        "include/boost/system/**/*.h",
    ]),
    includes = ["include"],
    visibility = ["//visibility:public"],
    linkopts = ["-lboost_system"],
)

cc_library(
    name = "boost_thread",
    hdrs = glob([
        "include/boost/thread/**/*.hpp",
        "include/boost/thread/**/*.h",
    ]),
    includes = ["include"],
    visibility = ["//visibility:public"],
    linkopts = ["-lboost_thread"],
)

cc_library(
    name = "boost_asio",
    hdrs = glob([
        "include/boost/asio/**/*.hpp",
        "include/boost/asio/**/*.h",
    ]),
    includes = ["include"],
    visibility = ["//visibility:public"],
    deps = [
        ":boost_system",
        ":boost_thread",
    ],
) 