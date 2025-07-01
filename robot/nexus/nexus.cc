#include "robot/nexus/nexus.h"
#include "robot/nexus/nexus_scheduler.h"

namespace robot::nexus {
namespace {
    // These constants are no longer needed here, they are in the python script.
}

Nexus::Nexus(const int& trigger_frequency):
stop_(false)
{
    scheduler_ = std::make_unique<NexusScheduler>(trigger_frequency);
    ai_executor_ = std::make_unique<AIExecutor>();
    LOG(INFO) << "Nexus construted.";
}

Nexus::~Nexus(){
    if (stop_.exchange(true)) {
        return;
    }
    LOG(INFO) << "Destroying Nexus...";
    scheduler_->Stop();
    if (main_thread_.joinable()) {
        main_thread_.join();
    }

    {
        ThreadPool local_pool(action_interfaces_.size());
        for(auto& interface : action_interfaces_){
            std::function<void()> job = [&interface](){
                LOG(INFO) << "Graceful shutdown for " << interface.first;
                std::visit([](auto&& arg){
                    arg->GracefulShutdown();
                }, interface.second);
            
            };
            local_pool.push_job(job);
        }
    } // local_pool is destroyed here, and its destructor waits for jobs.

    LOG(INFO) << "Nexus destroyed.";
}

bool Nexus::Init(){
    if (perception_interfaces_.empty() && action_interfaces_.empty()) {
        LOG(WARNING) << "No interfaces registered with Nexus.";
    }
    // TODO: Update this by using the model name and function name from the config.
    if (!ai_executor_->Init("ai.model", "generate_mock_ai_output")) {
        LOG(ERROR) << "Failed to initialize AI executor.";
        return false;
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
                LOG(INFO) << "Perception packet: " << packet->DebugString();
                perception_packet_queue_.push(std::move(packet));
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
        auto nexus_model_output_packet = ai_executor_->Inference(nexus_model_input_packet);
        
        // Process the action packets.
        for(const auto& action_packet : nexus_model_output_packet.action_packets()){
            auto it = action_interfaces_.find(action_packet.action_id());
            if(it != action_interfaces_.end()){
                std::visit([&action_packet](auto&& arg){
                    arg->SetAction(std::make_unique<NexusActionPacket>(action_packet));
                }, it->second);
            }
        }

        scheduler_->WaitForNextTrigger();
    }
}

void Nexus::SetTriggerFrequency(int frequency) {
    scheduler_->SetFrequency(frequency);
}

int Nexus::GetTriggerFrequency() const {    
    return scheduler_->GetFrequency();
}

}