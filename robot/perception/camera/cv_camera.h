#pragma once

#include <glog/logging.h>

#include <cstdint>
#include <memory>
#include <mutex>
#include <opencv2/videoio.hpp>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "robot/perception/interfaces/camera_interface.h"
#include "robot/perception/proto/perception.pb.h"
#include "robot/perception/proto/perception_packet.pb.h"

namespace robot::perception {

// Narrow capture boundary: tests substitute returning calls without opening devices.
class CameraCapture {
 public:
  virtual ~CameraCapture() = default;
  virtual bool Open(int index, int backend) = 0;
  virtual bool IsOpened() const = 0;
  virtual bool Set(int property, double value) = 0;
  virtual double Get(int property) const = 0;
  virtual bool Read(cv::Mat& frame) = 0;
  virtual void Release() = 0;
};

// Image sensor backed by an OpenCV capture device.
class CvCamera : public CameraInterface {
 public:
  explicit CvCamera(const robot::perception::SinglePerception& camera_config);
  CvCamera(const robot::perception::SinglePerception& config,
           std::unique_ptr<CameraCapture> capture);
  ~CvCamera() override;

  absl::Status Init() override;
  std::string GetId() override;
  absl::StatusOr<robot::perception::PerceptionPacket> GetData() override;
  absl::Status Teardown() override;

 private:
  const uint8_t MAX_CAMERA_OPEN_TRIES_ = 3;
  std::unique_ptr<CameraCapture> cap_;
  mutable std::mutex cap_mutex_;
  std::string id_;
  uint64_t camera_id_;
  robot::perception::OpenCvConfig opencv_config_;
  mutable robot::perception::PerceptionPacket reusable_packet_;  // Pre-allocated packet for reuse

  absl::Status OpenCameraLocked();
  absl::Status TeardownLocked();
};

}  // namespace robot::perception
