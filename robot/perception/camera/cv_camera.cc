#include "robot/perception/camera/cv_camera.h"
#include "robot/nexus/proto/nexus_packet.pb.h"
#include <vector>
#include <opencv2/imgcodecs.hpp>

namespace robot {
namespace perception {

CvCamera::CvCamera() {
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

void CvCamera::Capture() {
    // In this implementation, GetData() also captures the frame,
    // so this function can be a no-op.
}

::robot::nexus::NexusPerceptionPacket CvCamera::GetData() {
    cv::Mat frame;
    if (cap_.isOpened()) {
        cap_ >> frame;
    }

    ::robot::nexus::NexusPerceptionPacket packet;
    if (!frame.empty()) {
        std::vector<uchar> buffer;
        cv::imencode(".jpg", frame, buffer);
        packet.mutable_camera_perception()->set_image_data(buffer.data(), buffer.size());
    }

    // Set other fields in the packet as needed.
    packet.set_timestamp(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
    packet.set_perception_id("cv_camera_0"); // Example ID

    return packet;
}

}  // namespace perception
}  // namespace robot 