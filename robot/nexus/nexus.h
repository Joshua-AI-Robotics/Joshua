#pragma once

#include "robot/nexus/nexus_packet.h"
#include "robot/nexus/interface_variant.h"
#include "robot/nexus/nexus_scheduler.h"
#include "robot/nexus/proto/nexus_packet.pb.h"
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <chrono>
#include <functional>
#include <unordered_set>
#include <atomic>
#include <glog/logging.h>

namespace robot::nexus {

class Nexus {
public:
explicit Nexus(const int& trigger_frequency = 10);
~Nexus();
bool Init();
void Register(const ActionInterface& interface);
void Register(const PerceptionInterface& interface);
void Start();

private:
void run();

NexusScheduler scheduler_;
std::vector<ActionInterface> action_interfaces_;
std::vector<PerceptionInterface> perception_interfaces_;
struct ActionPacketCompare {
    bool operator()(const robot::nexus::NexusActionPacket& a, const robot::nexus::NexusActionPacket& b) const {
        return a.timestamp() > b.timestamp();
    }
};
struct PerceptionPacketCompare {
    bool operator()(const robot::nexus::NexusPerceptionPacket& a, const robot::nexus::NexusPerceptionPacket& b) const {
        return a.timestamp() > b.timestamp();
    }
};
std::priority_queue<NexusActionPacket, std::vector<NexusActionPacket>, ActionPacketCompare> action_packet_queue_;
std::priority_queue<NexusPerceptionPacket, std::vector<NexusPerceptionPacket>, PerceptionPacketCompare> perception_packet_queue_;
std::mutex queue_mutex_;
std::thread main_thread_;
std::atomic<bool> stop_;

};

} // namespace robot::nexus