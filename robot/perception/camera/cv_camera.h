#pragma once

#include "robot/perception/interfaces/camera_interface.h"
#include <opencv2/videoio.hpp>
#include <glog/logging.h>

namespace robot {
namespace perception {

class CvCamera : public CameraInterface {
public:
    CvCamera();
    ~CvCamera() override;

    void Capture() override;
    ::robot::nexus::NexusPerceptionPacket GetData() override;

private:
    cv::VideoCapture cap_;
};

}  // namespace perception
}  // namespace robot 