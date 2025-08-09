#pragma once

#include "config/proto/config.pb.h"
#include <map>
#include <set>
#include <vector>
#include <string>
#include <memory>
#include <atomic>
#include <any>
#include <optional>
#include <sys/types.h>

namespace node_generator {

struct NodeInfo {
    std::string node_type;
    std::string node_name;
    uint32_t node_id;
    pid_t pid;
    std::vector<std::string> publish_topics;
    std::vector<std::string> subscribe_topics;
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
    void IdentifyNodeTypes();
    bool CheckConfigIntegrity();
    void DetermineRequiredBuilds();

    pid_t LaunchNode(const std::string& node_type, uint32_t node_id,
                     const std::string& node_name);

    // Process management
    void SetupSignalHandlers();
    void CleanupAndExit(int exit_code);
    void MonitorChildProcesses();

    void GetTopicsForNode(const uint32_t node_id, std::vector<std::string>& publish_topics, std::vector<std::string>& subscribe_topics);

    std::string get_binary_path() const;

    // Map from node_id to node type.
    std::map<uint32_t, std::string> identified_nodes_;

    std::string config_path_;
    std::string repo_root_;
    config::Config config_;
    
    std::set<std::string> required_builds_;
    
    std::vector<NodeInfo> launched_nodes_;
    volatile bool shutdown_requested_;
    
    static NodeGenerator* instance_;
    static void SignalHandler(int sig);
};

} // namespace node_generator 
