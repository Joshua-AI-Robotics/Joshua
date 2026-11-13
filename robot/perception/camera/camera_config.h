#pragma once

#include "absl/status/status.h"
#include "robot/perception/proto/perception.pb.h"

namespace robot::perception {

// Check camera settings without loading OpenCV or opening a capture device.
inline absl::Status ValidateCameraConfig(const OpenCvConfig& config) {
  if (config.fourcc().size() != 4) {
    return absl::InvalidArgumentError(
        "Camera pixel format must contain exactly four characters (for example, MJPG or "
        "YUYV); set opencv_config.fourcc");
  }
  return absl::OkStatus();
}

}  // namespace robot::perception
