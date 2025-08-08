#pragma once

#include "config/proto/config.pb.h"
#include <map>
#include <set>
#include <vector>
#include <string>
#include <memory>
#include <atomic>
#include <sys/types.h>

namespace node_generator {

struct NodeInfo {
    std::string node_type;
    std::string node_name;
    uint32_t node_id;
    pid_t pid;
};

class NodeGenerator {
public:
    explicit NodeGenerator(const std::string& config_path);
    ~NodeGenerator();

    bool Initialize();
    bool BuildRequiredTargets();
    bool BuildRequiredTargets(std::atomic_bool& stop_flag);
    bool LaunchAllNodes();
    void MonitorNodes();
    void Shutdown();
    void GetLaunchedNodes(std::vector<NodeInfo>& nodes);

    size_t GetLaunchedNodeCount() const { return launched_nodes_.size(); }
    bool HasNodes() const { return !launched_nodes_.empty(); }

private:
    void GroupEntitiesByNodeId();
    bool CheckConfigIntegrity();
    void DetermineRequiredBuilds();

    pid_t LaunchPerceptionNode(const std::string& node_type, uint32_t node_id, 
                              const std::string& node_name);
    pid_t LaunchActionNode(const std::string& node_type, uint32_t node_id, 
                          const std::string& node_name);

    // Process management
    void SetupSignalHandlers();
    void CleanupAndExit(int exit_code);
    void MonitorChildProcesses();

    std::string GetBinaryPath() const;
    std::string GetPerceptionNodeTypePriority(const std::set<std::string>& sensor_types) const;
    std::string GetActionNodeTypePriority(const std::set<std::string>& action_types) const;

    std::string config_path_;
    std::string repo_root_;
    config::Config config_;
    
    std::map<uint32_t, std::set<std::string>> node_perception_types_;
    std::map<uint32_t, std::set<std::string>> node_action_types_;
    std::set<std::string> required_builds_;
    
    std::vector<NodeInfo> launched_nodes_;
    volatile bool shutdown_requested_;
    
    static NodeGenerator* instance_;
    static void SignalHandler(int sig);
};

} // namespace node_generator 
