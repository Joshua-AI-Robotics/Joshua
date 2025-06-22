#include "robot/perception/factory/camera_factory.h"
#include "robot/perception/interfaces/camera_interface.h"
#include <glog/logging.h>
#include <opencv2/highgui.hpp>

int main(int argc, char** argv) {
    google::InitGoogleLogging(argv[0]);
    FLAGS_logtostderr = 1;

    robot::perception::CameraFactory camera_factory;
    std::unique_ptr<robot::perception::CameraInterface> camera = camera_factory.CreateCamera();

    if (!camera) {
        LOG(ERROR) << "Failed to create camera.";
        return -1;
    }

    cv::namedWindow("Camera", cv::WINDOW_AUTOSIZE);

    while (true) {
        cv::Mat frame = camera->GetFrame();
        if (frame.empty()) {
            LOG(WARNING) << "Failed to capture frame.";
            break;
        }

        cv::imshow("Camera", frame);

        if (cv::waitKey(1) == 'q') {
            break;
        }
    }

    cv::destroyAllWindows();

    return 0;
} 