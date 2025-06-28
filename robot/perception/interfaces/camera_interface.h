#pragma once

#include "robot/nexus/proto/nexus_packet.pb.h"
#include <vector>
#include <memory>
#include <opencv2/opencv.hpp>

namespace robot::perception {

class CameraInterface {
public:
    CameraInterface() = default;
    virtual ~CameraInterface() = default;

    virtual cv::Mat Capture() = 0;
    virtual std::unique_ptr<::robot::nexus::NexusPerceptionPacket> GetData() = 0;
};

} // namespace robot::perception 