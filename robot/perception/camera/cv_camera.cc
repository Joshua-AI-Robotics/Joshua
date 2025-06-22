#include "robot/perception/camera/cv_camera.h"

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

cv::Mat CvCamera::GetFrame() {
    cv::Mat frame;
    if (cap_.isOpened()) {
        cap_ >> frame;
    }
    return frame;
}

}  // namespace perception
}  // namespace robot 