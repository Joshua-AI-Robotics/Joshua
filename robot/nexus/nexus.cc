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
            // Placeholder for getting data from the perception interface.
            // A std::visit pattern would be used here to handle the variant types.
            LOG(INFO) << "Triggering perception data acquisition.";
        }
        for(auto& interface : action_interfaces_){
            // Placeholder for getting data from the action interface.
            LOG(INFO) << "Triggering action data acquisition.";
        }


        std::unique_lock<std::mutex> lock(queue_mutex_);
        if(!packet_queue_.empty()){
            NexusPacket packet = packet_queue_.top();
            packet_queue_.pop();
            lock.unlock();

            LOG(INFO) << "Processing packet from " << packet.sensor_id;
            // TODO: Add packet processing logic here.
        } else {
            lock.unlock();
        }

        scheduler_.wait_for_next_trigger();
    }
}

}