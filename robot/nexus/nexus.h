#pragma once

#include "robot/nexus/nexus_packet.h"
#include "robot/nexus/interface_variant.h"
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <chrono>
#include <functional>
#include <unordered_set>
#include <atomic>
#include <memory>
#include <glog/logging.h>
#include <random>

namespace robot::nexus {

class NexusScheduler;

class Nexus {
public:
explicit Nexus(const int& trigger_frequency = 30);
~Nexus();
bool Init();
void Register(ActionInterface&& interface);
void Register(PerceptionInterface&& interface);
void Start();
void SetTriggerFrequency(int frequency);
int GetTriggerFrequency() const;

private:
void run();
NexusModelOutputPacket GenerateMockAIOutput(const NexusModelInputPacket& input_packet);

std::unique_ptr<NexusScheduler> scheduler_;
std::map<std::string, ActionInterface> action_interfaces_;
std::vector<PerceptionInterface> perception_interfaces_;

struct PerceptionPacketCompare {
    bool operator()(const std::shared_ptr<robot::nexus::NexusPerceptionPacket>& a, const std::shared_ptr<robot::nexus::NexusPerceptionPacket>& b) const {
        return a->timestamp() > b->timestamp();
    }
};
std::vector<std::unique_ptr<NexusActionPacket>> action_packet_queue_;
std::priority_queue<std::shared_ptr<NexusPerceptionPacket>, std::vector<std::shared_ptr<NexusPerceptionPacket>>, PerceptionPacketCompare> perception_packet_queue_;
std::mutex queue_mutex_;
std::thread main_thread_;
std::atomic<bool> stop_;

};

} // namespace robot::nexus