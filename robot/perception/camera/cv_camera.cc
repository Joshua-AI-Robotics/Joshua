#include "robot/perception/camera/cv_camera.h"
#include <vector>
#include <chrono>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/videoio.hpp>
#include <stdexcept>

namespace robot::perception {

CvCamera::CvCamera(const robot::perception::Camera& camera_config) {
    auto opencv_config = camera_config.opencv_config();
    camera_id_ = opencv_config.id();
    id_ = GetId();
    cap_.open(camera_id_, cv::CAP_V4L2);
    if (!cap_.isOpened()) {
        LOG(ERROR) << "ERROR: Could not open camera with id " << camera_id_;
        throw std::runtime_error("Could not open camera with id " + std::to_string(camera_id_));
    }
    bool set_fourcc = cap_.set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('M', 'J', 'P', 'G'));
    bool set_width = cap_.set(cv::CAP_PROP_FRAME_WIDTH, opencv_config.width());
    bool set_height = cap_.set(cv::CAP_PROP_FRAME_HEIGHT, opencv_config.height());
    bool set_fps = cap_.set(cv::CAP_PROP_FPS, opencv_config.fps());

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

robot::perception::PerceptionPacket CvCamera::GetData() {
    cv::Mat frame;
    cap_ >> frame;
    if (frame.empty()) {
        LOG(ERROR) << "Failed to capture an image from camera.";
        reusable_packet_.Clear();
        return reusable_packet_;
    }

    // Clear and populate the reusable packet
    reusable_packet_.Clear();
    reusable_packet_.set_perception_id(id_);
    reusable_packet_.set_timestamp_ns(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());

    // Populate ImageData fields
    auto* image_data = reusable_packet_.mutable_image();
    image_data->set_width(frame.cols);
    image_data->set_height(frame.rows);
    image_data->set_channels(frame.channels());
    image_data->set_encoding("bgr8");  // OpenCV default is BGR
    
    
    // Set image data using string assignment to be safe
    size_t data_size = frame.total() * frame.elemSize();
    
    // Create a string from the image data and assign it
    std::string image_string(reinterpret_cast<const char*>(frame.data), data_size);
    image_data->set_data(image_string);
        
    return reusable_packet_;
}

std::string CvCamera::GetId() {
    auto id = "cv_camera_" + std::to_string(camera_id_);
    return id;
}

}  // namespace robot::perception
