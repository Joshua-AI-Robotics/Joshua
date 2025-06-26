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

void Nexus::Register(ActionInterface&& interface) {
    std::string id;
    std::visit([&id](auto&& arg) {
        id = arg->GetId();
    }, interface);
    action_interfaces_.emplace(id, std::move(interface));
}

void Nexus::Register(PerceptionInterface&& interface) {
    perception_interfaces_.push_back(std::move(interface));
}

void Nexus::Start() {
    main_thread_ = std::thread(&Nexus::run, this);
}

void Nexus::run(){
    while(!stop_){
        // In this new design, the run loop itself triggers the data acquisition.
        for(auto& interface : perception_interfaces_){
            std::visit([this](auto&& arg) {
                auto packet = arg->GetData();
                perception_packet_queue_.push(std::move(packet));
                LOG(INFO) << "Perception packet processed.";
            }, interface);
        }
        // Process the perception packets and make the nexus_packet.
        NexusModelInputPacket nexus_model_input_packet;
        while(!perception_packet_queue_.empty()){
            auto perception_packet = perception_packet_queue_.top();
            perception_packet_queue_.pop();            
            nexus_model_input_packet.add_perception_packets()->CopyFrom(*perception_packet);
        }
    
        // Psudo code for the model input.
        nexus_model_input_packet.set_timestamp(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
        nexus_model_input_packet.set_model_input_id("example_model_input_0");
        LOG(INFO) << "Nexus model input packet passed to AI layer.";

        // Psudo code of the output from the AI layer.
        // Make fake action packets.
        LOG(INFO) << "Nexus model output packet received from AI layer.";
        NexusModelOutputPacket nexus_model_output_packet;
        nexus_model_output_packet.set_timestamp(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
        nexus_model_output_packet.set_model_output_id("example_model_output_0");
        for(int i = 1; i < 6; i++){
            NexusActionPacket nexus_action_packet;
            nexus_action_packet.set_timestamp(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
            nexus_action_packet.set_action_id(std::to_string(i));
            nexus_action_packet.mutable_sts3215_action()->set_position(2000);
            nexus_model_output_packet.add_action_packets()->CopyFrom(nexus_action_packet);
        }
        
        // Process the action packets.
        for(const auto& action_packet : nexus_model_output_packet.action_packets()){
            auto it = action_interfaces_.find(action_packet.action_id());
            if(it != action_interfaces_.end()){
                std::visit([&action_packet](auto&& arg){
                    arg->SetAction(std::make_unique<NexusActionPacket>(action_packet));
                }, it->second);
            }
        }

        scheduler_.wait_for_next_trigger();
    }
}

}