#pragma once

#include <glog/logging.h>

#include <any>
#include <cstdint>
#include <memory>
#include <opencv2/videoio.hpp>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "robot/perception/interfaces/camera_interface.h"
#include "robot/perception/proto/perception_packet.pb.h"

namespace robot::perception {

// TODO: Update the ID, and logic.
class CvCamera : public CameraInterface {
 public:
  CvCamera(const robot::perception::Camera& camera_config);
  ~CvCamera() = default;

  absl::Status Init() override;
  std::string GetId() override;
  absl::StatusOr<robot::perception::PerceptionPacket> GetData() override;
  absl::Status Teardown() override;

 private:
  const uint8_t MAX_CAMERA_OPEN_TRIES_ = 3;
  cv::VideoCapture cap_;
  cv::Mat last_frame_;
  std::string id_;
  uint64_t camera_id_;
  robot::perception::OpenCvConfig opencv_config_;
  mutable robot::perception::PerceptionPacket reusable_packet_;  // Pre-allocated packet for reuse

  absl::Status OpenCamera();
};

}  // namespace robot::perception
