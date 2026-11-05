#include "robot/perception/camera/cv_camera.h"

#include <chrono>
#include <opencv2/videoio.hpp>

namespace robot::perception {
CvCamera::CvCamera(const robot::perception::SinglePerception& camera_config) {
  opencv_config_ = camera_config.opencv_config();
  camera_id_ = opencv_config_.id();
  id_ = camera_config.sensor_name();
}

CvCamera::~CvCamera() {
  std::lock_guard<std::mutex> lock(cap_mutex_);
  const absl::Status status = TeardownLocked();
  if (!status.ok()) {
    LOG(ERROR) << "CvCamera teardown failed in destructor for " << id_ << ": " << status;
  }
}

absl::Status CvCamera::Init() {
  std::lock_guard<std::mutex> lock(cap_mutex_);

  absl::Status status = OpenCameraLocked();
  if (!status.ok()) {
    return status;
  }

  if (opencv_config_.fourcc().size() < 4) {
    return absl::Status(absl::StatusCode::kInvalidArgument, "FourCC is not specified");
  }
  bool set_fourcc = cap_.set(cv::CAP_PROP_FOURCC,
                             cv::VideoWriter::fourcc(opencv_config_.fourcc()[0],
                                                     opencv_config_.fourcc()[1],
                                                     opencv_config_.fourcc()[2],
                                                     opencv_config_.fourcc()[3]));
  bool set_width = cap_.set(cv::CAP_PROP_FRAME_WIDTH, opencv_config_.width());
  bool set_height = cap_.set(cv::CAP_PROP_FRAME_HEIGHT, opencv_config_.height());
  bool set_fps = cap_.set(cv::CAP_PROP_FPS, opencv_config_.fps());

  // Some drivers return false even on success.
  if (!set_fourcc || !set_width || !set_height || !set_fps) {
    return absl::Status(
        absl::StatusCode::kInternal,
        "Failed to configure camera " + id_ + ": fourcc=" + std::to_string(set_fourcc) +
            ", width=" + std::to_string(set_width) + ", height=" + std::to_string(set_height) +
            ", fps=" + std::to_string(set_fps));
  }

  LOG(INFO) << "Camera initialized successfully.";
  LOG(INFO) << "ID: " << id_;
  LOG(INFO) << "Camera resolution: " << cap_.get(cv::CAP_PROP_FRAME_WIDTH) << "x"
            << cap_.get(cv::CAP_PROP_FRAME_HEIGHT);
  LOG(INFO) << "Camera FPS: " << cap_.get(cv::CAP_PROP_FPS);

  return absl::OkStatus();
}

absl::Status CvCamera::Teardown() {
  std::lock_guard<std::mutex> lock(cap_mutex_);
  return TeardownLocked();
}

absl::Status CvCamera::TeardownLocked() {
  try {
    if (cap_.isOpened()) {
      cap_.release();
    }
  } catch (const std::exception& e) {
    return absl::Status(absl::StatusCode::kInternal,
                        "Failed to teardown camera " + id_ + ": " + e.what());
  } catch (...) {
    return absl::Status(absl::StatusCode::kInternal, "Unknown exception in camera " + id_);
  }
  return absl::OkStatus();
}

absl::StatusOr<robot::perception::PerceptionPacket> CvCamera::GetData() {
  std::lock_guard<std::mutex> lock(cap_mutex_);

  try {
    absl::Status status = OpenCameraLocked();
    if (!status.ok()) {
      return status;
    }

    cv::Mat frame;
    cap_ >> frame;

    if (frame.empty()) {
      reusable_packet_.Clear();
      return absl::Status(absl::StatusCode::kInternal,
                          "Failed to capture an image from camera with empty frame");
    }

    // Validate frame properties
    if (frame.cols <= 0 || frame.rows <= 0) {
      reusable_packet_.Clear();
      return absl::Status(absl::StatusCode::kInternal,
                          "Invalid frame dimensions for camera " + id_ + ": " +
                              std::to_string(frame.cols) + "x" + std::to_string(frame.rows));
    }

    if (frame.channels() != 3) {
      reusable_packet_.Clear();
      return absl::Status(absl::StatusCode::kInternal,
                          "Unexpected channels for camera " + id_ + ": " +
                              std::to_string(frame.channels()) + " (expected 3)");
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

    if (!frame.isContinuous()) {
      frame = frame.clone();
    }

    const size_t data_size = static_cast<size_t>(frame.total()) * frame.elemSize();

    if (data_size == 0) {
      reusable_packet_.Clear();
      return absl::Status(absl::StatusCode::kInternal,
                          "Failed to capture an image from camera with frame data size is 0");
    }

    if (frame.data == nullptr) {
      reusable_packet_.Clear();
      return absl::Status(absl::StatusCode::kInternal,
                          "Failed to capture an image from camera with frame data pointer is null");
    }

    image_data->mutable_data()->assign(reinterpret_cast<const char*>(frame.data), data_size);

    VLOG(1) << "Successfully captured frame from camera " << id_ << ": " << frame.cols << "x"
            << frame.rows << " (" << data_size << " bytes)";

    return reusable_packet_;

  } catch (const cv::Exception& e) {
    reusable_packet_.Clear();
    return absl::Status(absl::StatusCode::kInternal,
                        "OpenCV exception in camera " + id_ + ": " + e.what());
  } catch (const std::exception& e) {
    reusable_packet_.Clear();
    return absl::Status(absl::StatusCode::kInternal,
                        "Standard exception in camera " + id_ + ": " + e.what());
  } catch (...) {
    reusable_packet_.Clear();
    return absl::Status(absl::StatusCode::kInternal, "Unknown exception in camera " + id_);
  }
}

std::string CvCamera::GetId() {
  return id_;
}

absl::Status CvCamera::OpenCameraLocked() {
  absl::Status status;

  for (uint8_t i = 0; i < MAX_CAMERA_OPEN_TRIES_; i++) {
    if (cap_.isOpened()) {
      status = absl::OkStatus();
      break;
    } else {
      if (cap_.open(camera_id_, cv::CAP_V4L2)) {
        if (!cap_.set(cv::CAP_PROP_BUFFERSIZE, 1)) {
          LOG(ERROR) << "Could not set CAP_PROP_BUFFERSIZE to 1 for camera " << id_;
        }
      }
    }
  }
  if (cap_.isOpened()) {
    status = absl::OkStatus();
  } else {
    status = absl::Status(absl::StatusCode::kInternal,
                          "Could not open camera with id " + std::to_string(camera_id_));
  }

  return status;
}

}  // namespace robot::perception
