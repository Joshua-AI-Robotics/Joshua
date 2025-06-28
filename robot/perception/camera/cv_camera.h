#pragma once

#include "robot/perception/interfaces/camera_interface.h"
#include <opencv2/videoio.hpp>
#include <glog/logging.h>
#include <memory>

namespace robot::perception {

// TODO: Update the ID, and logic.
class CvCamera : public CameraInterface {
public:
    CvCamera();
    ~CvCamera() override;

    cv::Mat Capture() override;
    std::unique_ptr<::robot::nexus::NexusPerceptionPacket> GetData() override;

private:
    cv::VideoCapture cap_;
};

}  // namespace robot::perception
