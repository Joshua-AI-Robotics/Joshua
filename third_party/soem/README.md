# SOEM Bazel Wrapper

Joshua pins upstream SOEM through `MODULE.bazel` and overlays this Bazel build
file onto the release archive. The wrapper builds the Linux SOEM library only;
samples and non-Linux ports are intentionally excluded.

SOEM v2.0.0 expects CMake to generate `include/soem/ec_options.h`. The patch in
this directory adds that generated header with upstream default values so Bazel
can compile the library directly.

SOEM is dual-licensed under GPLv3 or a commercial license. Do not treat this
wrapper as a licensing decision for Joshua.
