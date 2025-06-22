workspace(name = "ubirobotics")

load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")
load("@bazel_tools//tools/build_defs/repo:local.bzl", "new_local_repository")

# glog
http_archive(
    name = "com_github_gflags_gflags",
    sha256 = "34af2f15cf7367513b352bdcd2493ab14ce43692d2dcd9dfc499492966c64dcf",
    strip_prefix = "gflags-2.2.2",
    urls = ["https://github.com/gflags/gflags/archive/v2.2.2.tar.gz"],
)

http_archive(
    name = "com_github_google_glog",
    sha256 = "c17d85c03ad9630006ef32c7be7c65656aba2e7e2fbfc82226b7e680c771fc88",
    strip_prefix = "glog-0.7.1",
    urls = ["https://github.com/google/glog/archive/v0.7.1.zip"],
)

# pybind11
http_archive(
  name = "pybind11_bazel",
  strip_prefix = "pybind11_bazel-34206c29f891dbd5f6f5face7b91664c2ff7185c",
  urls = ["https://github.com/pybind/pybind11_bazel/archive/34206c29f891dbd5f6f5face7b91664c2ff7185c.zip"],
  sha256 = "8d0b776ea5b67891f8585989d54aa34869fc12f14bf33f1dc7459458dd222e95",
)

http_archive(
  name = "pybind11",
  build_file = "@pybind11_bazel//:pybind11.BUILD",
  strip_prefix = "pybind11-a54eab92d265337996b8e4b4149d9176c2d428a6",
  urls = ["https://github.com/pybind/pybind11/archive/a54eab92d265337996b8e4b4149d9176c2d428a6.tar.gz"],
  sha256 = "c9375b7453bef1ba0106849c83881e6b6882d892c9fae5b2572a2192100ffb8a",
)

# load("@pybind11_bazel//:python_configure.bzl", "python_configure")
# python_configure(name = "local_config_python")


# Python rules
load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

http_archive(
    name = "rules_python",
    sha256 = "ca77768989a7f311186a29747e3e95c936a41dffac779aff6b443db22290d913",
    strip_prefix = "rules_python-0.36.0",
    url = "https://github.com/bazelbuild/rules_python/releases/download/0.36.0/rules_python-0.36.0.tar.gz",
)

http_archive(
    name = "rules_cc",
    sha256 = "2037875b9a4456dce4a79d112a8ae885bbc4aad968e6587dca6e64f3a0900cdf",
    strip_prefix = "rules_cc-0.0.9",
    urls = ["https://github.com/bazelbuild/rules_cc/releases/download/0.0.9/rules_cc-0.0.9.tar.gz"],
)

http_archive(
    name = "rules_proto",
    sha256 = "0e5c64a2599a6e26c6a03d6162242d231ecc0de219534c38cb4402171def21e8",
    strip_prefix = "rules_proto-7.0.2",
    url = "https://github.com/bazelbuild/rules_proto/releases/download/7.0.2/rules_proto-7.0.2.tar.gz",
)

# The proper load statements for this version
load("@rules_proto//proto:repositories.bzl", "rules_proto_dependencies")
rules_proto_dependencies()

load("@rules_proto//proto:toolchains.bzl", "rules_proto_toolchains")
rules_proto_toolchains()

load("@rules_proto//proto:setup.bzl", "rules_proto_setup")
rules_proto_setup()

http_archive(
    name = "com_google_protobuf",
    urls = ["https://github.com/protocolbuffers/protobuf/archive/v3.19.4.zip"],
    strip_prefix = "protobuf-3.19.4",
)

load("@com_google_protobuf//:protobuf_deps.bzl", "protobuf_deps")
protobuf_deps()

load("@rules_python//python:repositories.bzl", "python_register_toolchains", "py_repositories")
py_repositories()

python_register_toolchains(
    name = "python3_9",
    python_version = "3.9",
    register_coverage_tool = True,
)

load("@python3_9//:defs.bzl", "interpreter")
load("@rules_python//python:pip.bzl", "pip_parse")

pip_parse(
    name = "my_pip_install",
    requirements_lock = "//:requirements.txt",
    python_interpreter_target = "@python3_9_host//:python",
)

# Load dependencies from the generated repo
load("@my_pip_install//:requirements.bzl", "install_deps")
install_deps()

# Need to install boost library on the system.
# sudo apt install libboost-all-dev
new_local_repository(
    name = "boost",
    path = "/usr",  # Assuming Boost is installed in /usr/include and /usr/lib
    build_file = "//:boost.BUILD",
)

# libevdev
# sudo apt install libevdev-dev
new_local_repository(
    name = "libevdev",
    path = "/usr",  # or wherever your system libraries are
    build_file_content = """
cc_library(
    name = "libevdev",
    hdrs = glob(["include/libevdev-1.0/libevdev/*.h"]),
    includes = ["include/libevdev-1.0"],
    linkopts = ["-levdev"],
    visibility = ["//visibility:public"],
)
""",
)

# OpenCV
# sudo apt install libopencv-dev
new_local_repository(
    name = "opencv",
    path = "/usr",
    build_file = "//:opencv.BUILD",
)