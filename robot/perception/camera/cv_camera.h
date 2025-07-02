#pragma once

#include "robot/perception/interfaces/camera_interface.h"
#include <opencv2/videoio.hpp>
#include <glog/logging.h>
#include <memory>
#include "robot/perception/proto/perception.pb.h"

namespace robot::perception {

// TODO: Update the ID, and logic.
class CvCamera : public CameraInterface {
public:
    CvCamera(const robot::perception::Sensor& sensor_config);
    ~CvCamera() override;

    std::unique_ptr<robot::nexus::NexusPerceptionPacket> GetData() override;
    std::string GetId() override;

private:
    cv::VideoCapture cap_;
    cv::Mat last_frame_;
    std::string id_;
    uint64_t camera_id_;
};

}  // namespace robot::perception
