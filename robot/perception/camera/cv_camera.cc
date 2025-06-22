#include "robot/perception/camera/cv_camera.h"

namespace robot {
namespace perception {

CvCamera::CvCamera() {
    // Open the default camera (device 0).
    cap_.open(0);
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