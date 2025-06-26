#include "robot/config/config_utils.h"
#include "robot/config/robot.pb.h"
#include "robot/perception/factory/camera_factory.h"
#include "robot/perception/interfaces/camera_interface.h"
#include "robot/nexus/proto/nexus_packet.pb.h"
#include <glog/logging.h>
#include <opencv2/highgui.hpp>
#include <opencv2/imgcodecs.hpp>

int main(int argc, char** argv) {
    google::InitGoogleLogging(argv[0]);
    FLAGS_logtostderr = 1;

    // Load robot config.
    robot::Robot robot_config;
    try {
        robot_config = robot::config_util::LoadRobotConfig("robot/config/robot_config.pbtxt");
    } catch (const std::exception& e) {
        LOG(ERROR) << "Failed to load robot config: " << e.what();
        return -1;
    }

    // For this main, we only use the first camera.
    if (robot_config.perceptions().single_perception_size() == 0) {
        LOG(ERROR) << "No perceptions found in robot config.";
        return -1;
    }

    const robot::perception::Camera* camera_config = nullptr;
    for (const auto& perception : robot_config.perceptions().single_perception()) {
        if (perception.has_camera()) {
            camera_config = &perception.camera();
            break;
        }
    }

    if (camera_config == nullptr) {
        LOG(ERROR) << "No cameras found in robot config.";
        return -1;
    }

    robot::perception::CameraFactory camera_factory;
    std::unique_ptr<robot::perception::CameraInterface> camera = camera_factory.CreateCamera(*camera_config);

    if (!camera) {
        LOG(ERROR) << "Failed to create camera.";
        return -1;
    }

    cv::namedWindow("Camera", cv::WINDOW_AUTOSIZE);

    while (true) {
        camera->Capture();
        auto packet = camera->GetData();
        if (!packet) {
            LOG(WARNING) << "Failed to get data packet.";
            break;
        }

        const auto& image_data = packet->camera_perception().image_data();
        std::vector<char> data(image_data.begin(), image_data.end());
        cv::Mat frame = cv::imdecode(data, cv::IMREAD_COLOR);

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