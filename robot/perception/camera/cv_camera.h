#pragma once

#include "robot/perception/interfaces/camera_interface.h"
#include <opencv2/videoio.hpp>
#include <opencv2/core/mat.hpp>
#include <glog/logging.h>
#include <memory>

namespace robot::perception {

// TODO: Update the ID, and logic.
class CvCamera : public CameraInterface {
public:
    CvCamera();
    ~CvCamera() override;

    std::unique_ptr<robot::nexus::NexusPerceptionPacket> GetData() override;
    std::string GetId() override;

private:
    cv::VideoCapture cap_;
    cv::Mat last_frame_;
};

}  // namespace robot::perception
