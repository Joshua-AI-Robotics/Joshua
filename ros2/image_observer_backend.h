#pragma once
#include <cstdint>
#include <functional>
#include <opencv2/core.hpp>
#include <vector>
namespace ros2_utils {
// Narrow codec boundary for deterministic binding-race/failure tests. The executable
// always uses the fixed JPEG quality-80 implementation; no runtime codec options.
using JpegEncoder = std::function<bool(const cv::Mat&, std::vector<uint8_t>&)>;
// Internal test seam after a named manifest inode is pinned and validated.
// Production leaves it empty; no executable option can enable it.
using ManifestReadHook = std::function<void()>;
int RunImageObserver(int argc,
                     char** argv,
                     JpegEncoder encoder = {},
                     ManifestReadHook manifest_read_hook = {});
}  // namespace ros2_utils
