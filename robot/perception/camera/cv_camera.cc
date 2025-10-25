#include "robot/perception/camera/cv_camera.h"

#include <chrono>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/videoio.hpp>
#include <stdexcept>
#include <vector>

namespace robot::perception {

CvCamera::CvCamera(const robot::perception::Camera& camera_config) {
  opencv_config_ = camera_config.opencv_config();
  camera_id_ = opencv_config_.id();
  id_ = GetId();
}

absl::Status CvCamera::Init() {
  cap_.open(camera_id_, cv::CAP_V4L2);
  if (!cap_.isOpened()) {
    LOG(ERROR) << "ERROR: Could not open camera with id " << camera_id_;
    return absl::Status(absl::StatusCode::kInternal,
                        "Could not open camera with id " + std::to_string(camera_id_));
  }

  // TODO: Remove this hardcoded values.
  bool set_fourcc = cap_.set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('M', 'J', 'P', 'G'));
  bool set_width = cap_.set(cv::CAP_PROP_FRAME_WIDTH, opencv_config_.width());
  bool set_height = cap_.set(cv::CAP_PROP_FRAME_HEIGHT, opencv_config_.height());
  bool set_fps = cap_.set(cv::CAP_PROP_FPS, opencv_config_.fps());

  // Some drivers return false even on success.
  if (!set_fourcc || !set_width || !set_height || !set_fps) {
    LOG(ERROR) << "Setting camera resolution returned false. "
               << "set_fourcc=" << std::boolalpha << set_fourcc << ", "
               << "set_width=" << std::boolalpha << set_width << ", "
               << "set_height=" << std::boolalpha << set_height << ", "
               << "set_fps=" << std::boolalpha << set_fps;
    return absl::Status(absl::StatusCode::kInternal, "Failed to set camera resolution.");
  }

  LOG(INFO) << "Camera initialized successfully.";
  LOG(INFO) << "ID: " << id_;
  LOG(INFO) << "Camera resolution: " << cap_.get(cv::CAP_PROP_FRAME_WIDTH) << "x"
            << cap_.get(cv::CAP_PROP_FRAME_HEIGHT);
  LOG(INFO) << "Camera FPS: " << cap_.get(cv::CAP_PROP_FPS);

  return absl::OkStatus();
}

absl::Status CvCamera::Teardown() {
  try {
    if (cap_.isOpened()) {
      cap_.release();
    }
  } catch (const std::exception& e) {
    LOG(ERROR) << "Error: " << e.what();
    return absl::Status(absl::StatusCode::kInternal, "Failed to teardown camera.");
  } catch (...) {
    LOG(ERROR) << "Unknown exception in camera " << id_;
    return absl::Status(absl::StatusCode::kInternal, "Unknown exception in camera");
  }
  return absl::OkStatus();
}

absl::StatusOr<robot::perception::PerceptionPacket> CvCamera::GetData() {
  try {
    // Check if camera is still open
    if (!cap_.isOpened()) {
      LOG(ERROR) << "Camera " << id_ << " is not open. Attempting to reopen...";
      cap_.open(camera_id_, cv::CAP_V4L2);
      if (!cap_.isOpened()) {
        LOG(ERROR) << "Failed to reopen camera " << id_ << " with id " << camera_id_;
        reusable_packet_.Clear();
        return absl::Status(absl::StatusCode::kInternal, "Failed to reopen camera");
      }
      LOG(INFO) << "Successfully reopened camera " << id_;
    }

    cv::Mat frame;
    cap_ >> frame;

    if (frame.empty()) {
      LOG(ERROR) << "Failed to capture an image from camera " << id_
                 << " (camera_id: " << camera_id_ << ")";
      reusable_packet_.Clear();
      return absl::Status(absl::StatusCode::kInternal,
                          "Failed to capture an image from camera with empty frame");
    }

    // Validate frame properties
    if (frame.cols <= 0 || frame.rows <= 0) {
      LOG(ERROR) << "Invalid frame dimensions from camera " << id_ << ": " << frame.cols << "x"
                 << frame.rows;
      reusable_packet_.Clear();
      return absl::Status(absl::StatusCode::kInternal,
                          "Failed to capture an image from camera with invalid frame dimensions");
    }

    if (frame.channels() != 3) {
      LOG(ERROR) << "Unexpected number of channels from camera " << id_ << ": " << frame.channels()
                 << " (expected 3 for BGR)";
      reusable_packet_.Clear();
      return absl::Status(
          absl::StatusCode::kInternal,
          "Failed to capture an image from camera with unexpected number of channels");
    }

    // Clear and populate the reusable packet
    reusable_packet_.Clear();
    reusable_packet_.set_perception_id(id_);
    reusable_packet_.set_timestamp_ns(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                          std::chrono::steady_clock::now().time_since_epoch())
                                          .count());

    // Populate ImageData fields
    auto* image_data = reusable_packet_.mutable_image();
    image_data->set_width(frame.cols);
    image_data->set_height(frame.rows);
    image_data->set_channels(frame.channels());
    image_data->set_encoding("bgr8");  // OpenCV default is BGR

    // Set image data using string assignment to be safe
    size_t data_size = frame.total() * frame.elemSize();

    if (data_size == 0) {
      LOG(ERROR) << "Frame data size is 0 for camera " << id_;
      reusable_packet_.Clear();
      return absl::Status(absl::StatusCode::kInternal,
                          "Failed to capture an image from camera with frame data size is 0");
    }

    if (frame.data == nullptr) {
      LOG(ERROR) << "Frame data pointer is null for camera " << id_;
      reusable_packet_.Clear();
      return absl::Status(absl::StatusCode::kInternal,
                          "Failed to capture an image from camera with frame data pointer is null");
    }

    // Create a string from the image data and assign it
    std::string image_string(reinterpret_cast<const char*>(frame.data), data_size);
    image_data->set_data(image_string);

    LOG(INFO) << "Successfully captured frame from camera " << id_ << ": " << frame.cols << "x"
              << frame.rows << " (" << data_size << " bytes)";

    return reusable_packet_;

  } catch (const cv::Exception& e) {
    LOG(ERROR) << "OpenCV exception in camera " << id_ << ": " << e.what();
    reusable_packet_.Clear();
    return absl::Status(absl::StatusCode::kInternal, "OpenCV exception in camera");
  } catch (const std::exception& e) {
    LOG(ERROR) << "Standard exception in camera " << id_ << ": " << e.what();
    reusable_packet_.Clear();
    return absl::Status(absl::StatusCode::kInternal, "Standard exception in camera");
  } catch (...) {
    LOG(ERROR) << "Unknown exception in camera " << id_;
    reusable_packet_.Clear();
    return absl::Status(absl::StatusCode::kInternal, "Unknown exception in camera");
  }
}

std::string CvCamera::GetId() {
  auto id = "cv_camera_" + std::to_string(camera_id_);
  return id;
}

}  // namespace robot::perception
