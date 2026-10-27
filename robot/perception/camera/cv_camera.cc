#include "robot/perception/camera/cv_camera.h"

#include <chrono>
#include <climits>
#include <opencv2/videoio.hpp>
#include <thread>

namespace robot::perception {

namespace {
class OpenCvCapture final : public CameraCapture {
 public:
  bool Open(int index, int backend) override {
    return capture_.open(index, backend);
  }
  bool IsOpened() const override {
    return capture_.isOpened();
  }
  bool Set(int property, double value) override {
    return capture_.set(property, value);
  }
  double Get(int property) const override {
    return capture_.get(property);
  }
  bool Read(cv::Mat& frame) override {
    return capture_.read(frame);
  }
  void Release() override {
    capture_.release();
  }

 private:
  cv::VideoCapture capture_;
};
}  // namespace
CvCamera::CvCamera(const SinglePerception& config)
    : CvCamera(config, std::make_unique<OpenCvCapture>()) {}
CvCamera::CvCamera(const SinglePerception& config, std::unique_ptr<CameraCapture> capture)
    : cap_(std::move(capture)),
      id_(config.sensor_name()),
      camera_id_(config.opencv_config().id()),
      opencv_config_(config.opencv_config()) {}

CvCamera::~CvCamera() {
  std::lock_guard<std::mutex> lock(cap_mutex_);
  const absl::Status status = TeardownLocked();
  if (!status.ok()) {
    LOG(ERROR) << "CvCamera teardown failed in destructor for " << id_ << ": " << status;
  }
}

absl::Status CvCamera::Init() {
  std::lock_guard<std::mutex> lock(cap_mutex_);

  try {
    return OpenCameraLocked();
  } catch (...) {
    (void)TeardownLocked();
    return absl::InternalError("Camera initialization failed.");
  }
}

absl::Status CvCamera::Teardown() {
  std::lock_guard<std::mutex> lock(cap_mutex_);
  return TeardownLocked();
}

absl::Status CvCamera::TeardownLocked() {
  try {
    if (cap_ && cap_->IsOpened()) {
      cap_->Release();
    }
  } catch (const std::exception& e) {
    LOG(ERROR) << "Error: " << e.what();
    return absl::Status(absl::StatusCode::kInternal,
                        "Failed to teardown camera " + id_ + ": " + e.what());
  } catch (...) {
    LOG(ERROR) << "Unknown exception in camera " << id_;
    return absl::Status(absl::StatusCode::kInternal, "Unknown exception in camera");
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
    const bool captured = cap_->Read(frame);

    if (!captured || frame.empty()) {
      LOG(ERROR) << "Failed to capture an image from camera " << id_
                 << " (camera_id: " << camera_id_ << ")";
      reusable_packet_.Clear();
      (void)TeardownLocked();
      return absl::Status(absl::StatusCode::kInternal,
                          "Failed to capture an image from camera with empty frame");
    }

    // Host capture-completion estimate; OpenCV exposes no exposure timestamp here.
    const double acquisition_time = RobotTime();

    // Validate frame properties
    if (frame.cols <= 0 || frame.rows <= 0) {
      LOG(ERROR) << "Invalid frame dimensions from camera " << id_ << ": " << frame.cols << "x"
                 << frame.rows;
      reusable_packet_.Clear();
      (void)TeardownLocked();
      return absl::Status(absl::StatusCode::kInternal,
                          "Failed to capture an image from camera with invalid frame dimensions");
    }

    if (frame.channels() != 3 || frame.depth() != CV_8U) {
      LOG(ERROR) << "Unexpected number of channels from camera " << id_ << ": " << frame.channels()
                 << " (expected 3 for BGR)";
      reusable_packet_.Clear();
      (void)TeardownLocked();
      return absl::Status(
          absl::StatusCode::kInternal,
          "Failed to capture an image from camera with unexpected number of channels");
    }

    // Clear and populate the reusable packet
    reusable_packet_.Clear();
    reusable_packet_.set_perception_id(id_);
    reusable_packet_.set_timestamp(acquisition_time);

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
      LOG(ERROR) << "Frame data size is 0 for camera " << id_;
      reusable_packet_.Clear();
      (void)TeardownLocked();
      return absl::Status(absl::StatusCode::kInternal,
                          "Failed to capture an image from camera with frame data size is 0");
    }

    if (frame.data == nullptr) {
      LOG(ERROR) << "Frame data pointer is null for camera " << id_;
      reusable_packet_.Clear();
      (void)TeardownLocked();
      return absl::Status(absl::StatusCode::kInternal,
                          "Failed to capture an image from camera with frame data pointer is null");
    }

    image_data->mutable_data()->assign(reinterpret_cast<const char*>(frame.data), data_size);

    VLOG(1) << "Successfully captured frame from camera " << id_ << ": " << frame.cols << "x"
            << frame.rows << " (" << data_size << " bytes)";

    return reusable_packet_;

  } catch (const cv::Exception& e) {
    LOG(ERROR) << "OpenCV exception in camera " << id_ << ": " << e.what();
    reusable_packet_.Clear();
    (void)TeardownLocked();
    return absl::Status(absl::StatusCode::kInternal,
                        "OpenCV exception in camera " + id_ + ": " + e.what());
  } catch (const std::exception& e) {
    LOG(ERROR) << "Standard exception in camera " << id_ << ": " << e.what();
    reusable_packet_.Clear();
    (void)TeardownLocked();
    return absl::Status(absl::StatusCode::kInternal, "Standard exception in camera");
  } catch (...) {
    LOG(ERROR) << "Unknown exception in camera " << id_;
    reusable_packet_.Clear();
    (void)TeardownLocked();
    return absl::Status(absl::StatusCode::kInternal, "Unknown exception in camera");
  }
}

std::string CvCamera::GetId() {
  return id_;
}

absl::Status CvCamera::OpenCameraLocked() {
  const auto& c = opencv_config_;
  if (!cap_ || camera_id_ > INT_MAX || c.width() == 0 || c.height() == 0 || c.fps() == 0 ||
      c.width() > INT_MAX || c.height() > INT_MAX || c.fps() > INT_MAX)
    return absl::InvalidArgumentError(
        "Camera runtime requires index <= INT_MAX, positive dimensions and FPS fitting int.");
  // Defend direct driver calls; complete preset validation belongs to config::ValidateConfig.
  if (c.fourcc().size() != 4)
    return absl::InvalidArgumentError("OpenCV FourCC requires exactly four characters");
  {
    for (unsigned char ch : c.fourcc())
      if (ch < 32 || ch > 126)
        return absl::InvalidArgumentError("FourCC must contain printable ASCII bytes.");
  }
  if (cap_->IsOpened()) return absl::OkStatus();
  for (int attempt = 0; attempt < MAX_CAMERA_OPEN_TRIES_; ++attempt) {
    if (attempt) std::this_thread::sleep_for(std::chrono::milliseconds(25));
    if (!cap_->Open(static_cast<int>(camera_id_), cv::CAP_V4L2)) continue;
    bool okay = true;
    if (!c.fourcc().empty())
      okay = cap_->Set(
          cv::CAP_PROP_FOURCC,
          cv::VideoWriter::fourcc(c.fourcc()[0], c.fourcc()[1], c.fourcc()[2], c.fourcc()[3]));
    okay = cap_->Set(cv::CAP_PROP_FRAME_WIDTH, c.width()) && okay;
    okay = cap_->Set(cv::CAP_PROP_FRAME_HEIGHT, c.height()) && okay;
    okay = cap_->Set(cv::CAP_PROP_FPS, c.fps()) && okay;
    // Match the capture foundation: keep a second buffer queued for continuous capture.
    (void)cap_->Set(cv::CAP_PROP_BUFFERSIZE, 2);
    if (!okay) {
      cap_->Release();
      return absl::InternalError("Camera rejected requested capture settings.");
    }
    LOG(INFO) << "Camera " << id_ << " negotiated " << cap_->Get(cv::CAP_PROP_FRAME_WIDTH) << "x"
              << cap_->Get(cv::CAP_PROP_FRAME_HEIGHT) << " at " << cap_->Get(cv::CAP_PROP_FPS)
              << " FPS";
    return absl::OkStatus();
  }
  return absl::UnavailableError("Could not open camera after three attempts.");
}

}  // namespace robot::perception
