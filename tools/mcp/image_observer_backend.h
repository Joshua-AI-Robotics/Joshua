#pragma once
#include <functional>
#include <opencv2/core.hpp>
#include <vector>
namespace joshua::mcp {
// Test seam for codec failures and concurrent candidate edits. Production uses fixed JPEG settings.
using JpegEncoder = std::function<bool(const cv::Mat&, std::vector<uint8_t>&)>;
int RunImageObserver(int argc, char** argv, JpegEncoder encoder = {});
}  // namespace joshua::mcp
