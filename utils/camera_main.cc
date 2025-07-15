#include "config/config_utils.h"
#include "config/proto/config.pb.h"
#include "robot/perception/factory/perception_factory.h"
#include "robot/perception/interfaces/camera_interface.h"
#include "robot/nexus/proto/nexus_packet.pb.h"
#include <glog/logging.h>
#include <opencv2/highgui.hpp>
#include <opencv2/imgcodecs.hpp>

int main(int argc, char** argv) {
    google::InitGoogleLogging(argv[0]);
    FLAGS_logtostderr = 1;

    // Load robot config.
    config::Config config;
    try {
        config = config::config_util::LoadConfig("config/config_preset/so100_with_example_ai.pbtxt");
    } catch (const std::exception& e) {
        LOG(ERROR) << "Failed to load config: " << e.what();
        return -1;
    }
    const auto& robot_config = config.robot();

    // For this main, we only use the first camera.
    if (robot_config.perceptions().single_perception_size() == 0) {
        LOG(ERROR) << "No perceptions found in robot config.";
        return -1;
    }

    const robot::perception::Sensor* sensor_config = nullptr;
    for (const auto& perception : robot_config.perceptions().single_perception()) {
        if (perception.has_sensor() && perception.sensor().sensor_type() == robot::perception::SensorType::CAMERA) {
            sensor_config = &perception.sensor();
            break;
        }
    }

    if (sensor_config == nullptr) {
        LOG(ERROR) << "No cameras found in robot config.";
        return -1;
    }

    robot::perception::PerceptionFactory perception_factory;
    std::unique_ptr<robot::perception::PerceptionInterface> camera = perception_factory.CreatePerception(*sensor_config);

    if (!camera) {
        LOG(ERROR) << "Failed to create camera.";
        return -1;
    }

    cv::namedWindow("Camera", cv::WINDOW_AUTOSIZE);

    while (true) {
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