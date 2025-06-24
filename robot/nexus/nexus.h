#pragma once

#include "robot/nexus/nexus_packet.h"
#include "robot/nexus/interface_variant.h"
#include <unordered_map>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <chrono>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <atomic>

namespace robot::nexus {

class Nexus {
public:
Nexus();
~Nexus();
bool Init();
bool Register();

private:
void run();

int trigger_frequency_;
std::vector<InterfaceVariant> interfaces_;
// TODO: Update this to shared memory.
std::priority_queue<NexusPacket, std::vector<NexusPacket>, std::greater<NexusPacket>> packet_queue_;
};

} // namespace robot::nexus