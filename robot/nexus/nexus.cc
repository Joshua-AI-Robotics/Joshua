#include "robot/nexus/nexus.h"

namespace robot::nexus {

Nexus::Nexus(const int& trigger_frequency):
scheduler_(trigger_frequency), stop_(false)
{
    LOG(INFO) << "Nexus construted.";
}

Nexus::~Nexus(){
    stop_ = true;
    if (main_thread_.joinable()) {
        main_thread_.join();
    }
}

bool Nexus::Init(){
    if (perception_interfaces_.empty() && action_interfaces_.empty()) {
        LOG(WARNING) << "No interfaces registered with Nexus.";
    }
    stop_ = false;
    return true;
}

void Nexus::Register(const ActionInterface& interface) {
    action_interfaces_.push_back(interface);
}

void Nexus::Register(const PerceptionInterface& interface) {
    perception_interfaces_.push_back(interface);
}

void Nexus::Start() {
    main_thread_ = std::thread(&Nexus::run, this);
}

void Nexus::run(){
    while(!stop_){
        // In this new design, the run loop itself triggers the data acquisition.
        for(auto& interface : perception_interfaces_){
            std::visit([this](auto&& arg) {
                NexusPerceptionPacket packet = arg->GetData();
                perception_packet_queue_.push(packet);
                // TODO: Replace to actual process logic.
                LOG(INFO) << "Perception packet processed.";
                perception_packet_queue_.pop();
            }, interface);
        }

        // for(auto& interface : action_interfaces_){
        //     // Placeholder for getting data from the action interface.
        //     LOG(INFO) << "Triggering action data acquisition.";
        // }

        scheduler_.wait_for_next_trigger();
    }
}

}