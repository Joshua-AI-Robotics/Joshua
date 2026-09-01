#pragma once

#include <glog/logging.h>

#include <cstdint>
#include <mutex>
#include <opencv2/videoio.hpp>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "robot/perception/interfaces/perception_interface.h"
#include "robot/perception/proto/perception.pb.h"
#include "robot/perception/proto/perception_packet.pb.h"

namespace robot::perception {

// SensorType::IMAGE over a V4L2 device. A camera multiplexes nothing, so
// it is not a board: it owns its own device handle and is selected by the
// presence of OpenCvConfig (docs/BOARD_LAYER_RFC.md §5.3).
// TODO: Update the ID, and logic.
class CvCamera : public PerceptionInterface {
 public:
  explicit CvCamera(const robot::perception::Sensor& sensor_config);
  ~CvCamera() override;

  absl::Status Init() override;
  std::string GetId() override;
  absl::StatusOr<robot::perception::PerceptionPacket> GetData() override;
  absl::Status Teardown() override;

 private:
  const uint8_t MAX_CAMERA_OPEN_TRIES_ = 3;
  cv::VideoCapture cap_;
  mutable std::mutex cap_mutex_;
  std::string id_;
  uint64_t camera_id_;
  robot::perception::OpenCvConfig opencv_config_;
  mutable robot::perception::PerceptionPacket reusable_packet_;  // Pre-allocated packet for reuse

  absl::Status OpenCameraLocked();
  absl::Status TeardownLocked();
};

}  // namespace robot::perception
