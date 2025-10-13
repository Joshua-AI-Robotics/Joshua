#include "node_generator.h"
#include <glog/logging.h>
#include <gflags/gflags.h>

DEFINE_string(config, "config/config_preset/so100_teleoperate.pbtxt", "Path to the robot config file");

int main(int argc, char* argv[]) {
    google::InitGoogleLogging(argv[0]);
    FLAGS_logtostderr = 1;
    gflags::ParseCommandLineFlags(&argc, &argv, true);    

    LOG(INFO) << "Starting Node Generator with config: " << FLAGS_config;

    try {
        node_generator::NodeGenerator node_generator(FLAGS_config);
        
        if (!node_generator.Initialize().ok()) {
            LOG(ERROR) << "Failed to initialize NodeGenerator";
            return 1;
        }
        
        if (!node_generator.LaunchAllNodes().ok()) {
            LOG(ERROR) << "Failed to launch nodes";
            return 1;
        }
        
        if (!node_generator.has_nodes()) {
            LOG(WARNING) << "No nodes were launched successfully";
            return 1;
        }
        
        auto res = node_generator.MonitorNodes();
        if (!res.ok()) {
            LOG(ERROR) << "Failed to monitor nodes";
            return 1;
        }
    } catch (const std::exception& e) {
        LOG(ERROR) << "NodeGenerator error: " << e.what();
        return 1;
    }
    
    LOG(INFO) << "NodeGenerator completed successfully";
    return 0;
}
