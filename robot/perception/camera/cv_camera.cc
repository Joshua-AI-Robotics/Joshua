#include "robot/perception/camera/cv_camera.h"
#include "robot/nexus/proto/nexus_packet.pb.h"
#include <vector>
#include <chrono>
#include <opencv2/imgcodecs.hpp>

namespace robot::perception {

CvCamera::CvCamera(const robot::perception::Sensor& sensor_config) {
    auto camera_config = sensor_config.camera_config();
    camera_id_ = camera_config.id();
    id_ = GetId();
    // Open the default camera (device 0).
    cap_.open(0);

    // cap_.set(cv::CAP_PROP_FRAME_WIDTH, 1920);
    // cap_.set(cv::CAP_PROP_FRAME_HEIGHT, 1080);
    // cap_.set(cv::CAP_PROP_FPS, 5);

    // LOG(INFO) <<  cap_.get(cv::CAP_PROP_FRAME_WIDTH);
    // LOG(INFO) <<  cap_.get(cv::CAP_PROP_FRAME_HEIGHT);
    // LOG(INFO) <<  cap_.get(cv::CAP_PROP_FPS);

    
    if (!cap_.isOpened()) {
        // TODO: Replace with a proper logging mechanism.
        LOG(ERROR) << "ERROR: Could not open camera";
    }
}

CvCamera::~CvCamera() {
    if (cap_.isOpened()) {
        cap_.release();
    }
}

std::unique_ptr<robot::nexus::NexusPerceptionPacket> CvCamera::GetData() {
    auto packet = std::make_unique<robot::nexus::NexusPerceptionPacket>();
    // Capture the frame.
    cv::Mat frame;
    cap_ >> frame;
    if (frame.empty()) {
        LOG(ERROR) << "Failed to capture an image from camera.";
        return nullptr;
    }
    std::vector<uchar> buffer;
    cv::imencode(".jpg", frame, buffer);
    packet->mutable_camera_perception()->set_image_data(buffer.data(), buffer.size());

    // Set other fields in the packet as needed.
    packet->set_timestamp(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
    packet->set_perception_id(id_);

    return packet;
}

std::string CvCamera::GetId() {
    auto id = "cv_camera_" + std::to_string(camera_id_);
    return id;
}

}  // namespace robot::perception
