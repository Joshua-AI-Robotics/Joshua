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
#include <glog/logging.h>

namespace robot::nexus {

class Nexus {
public:
explicit Nexus(const int& trigger_frequency = 10);
~Nexus();
bool Init();
bool Register();

private:
void run();

int trigger_frequency_;
std::vector<ActionInterface> action_interfaces_;
std::vector<PerceptionInterface> perception_interfaces_;
// TODO: Update this to shared memory.
std::priority_queue<NexusPacket, std::vector<NexusPacket>, std::greater<NexusPacket>> packet_queue_;
std::thread reading_perception_thread_;
std::thread reading_action_thread_;
std::atomic<bool> stop_;

};

} // namespace robot::nexus