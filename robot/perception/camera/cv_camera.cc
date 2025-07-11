#include "robot/perception/camera/cv_camera.h"
#include "robot/nexus/proto/nexus_packet.pb.h"
#include <vector>
#include <chrono>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/videoio.hpp>
#include <stdexcept>

namespace robot::perception {

CvCamera::CvCamera(const robot::perception::Sensor& sensor_config) {
    auto camera_config = sensor_config.camera_config();
    camera_id_ = camera_config.id();
    id_ = GetId();
    cap_.open(camera_id_, cv::CAP_V4L2);
    if (!cap_.isOpened()) {
        LOG(ERROR) << "ERROR: Could not open camera with id " << camera_id_;
        throw std::runtime_error("Could not open camera with id " + std::to_string(camera_id_));
    }
    bool set_fourcc = cap_.set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('M', 'J', 'P', 'G'));
    bool set_width = cap_.set(cv::CAP_PROP_FRAME_WIDTH, camera_config.width());
    bool set_height = cap_.set(cv::CAP_PROP_FRAME_HEIGHT, camera_config.height());
    bool set_fps = cap_.set(cv::CAP_PROP_FPS, camera_config.fps());

    // Some drivers return false even on success.
    if (!set_fourcc || !set_width || !set_height || !set_fps) {
        LOG(ERROR) << "Setting camera resolution returned false. "
                     << "set_fourcc=" << std::boolalpha << set_fourcc << ", "
                     << "set_width=" << std::boolalpha << set_width << ", "
                     << "set_height=" << std::boolalpha << set_height << ", "
                     << "set_fps=" << std::boolalpha << set_fps;
        throw std::runtime_error("Failed to set camera resolution.");
    }

    LOG(INFO) << "ID: " << id_;
    LOG(INFO) << "Camera resolution: " << cap_.get(cv::CAP_PROP_FRAME_WIDTH) << "x" << cap_.get(cv::CAP_PROP_FRAME_HEIGHT);
    LOG(INFO) << "Camera FPS: " << cap_.get(cv::CAP_PROP_FPS);
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
