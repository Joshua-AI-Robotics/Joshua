#pragma once

#include <any>
#include "robot/perception/interfaces/camera_interface.h"
#include "robot/perception/proto/perception_packet.pb.h"
#include <opencv2/videoio.hpp>
#include <glog/logging.h>
#include <memory>

namespace robot::perception {

// TODO: Update the ID, and logic.
class CvCamera : public CameraInterface {
public:
    CvCamera(const robot::perception::Camera& camera_config);
    ~CvCamera() override;

    robot::perception::PerceptionPacket GetData() override;
    std::string GetId() override;

private:
    cv::VideoCapture cap_;
    cv::Mat last_frame_;
    std::string id_;
    uint64_t camera_id_;
    mutable robot::perception::PerceptionPacket reusable_packet_;  // Pre-allocated packet for reuse
};

}  // namespace robot::perception
