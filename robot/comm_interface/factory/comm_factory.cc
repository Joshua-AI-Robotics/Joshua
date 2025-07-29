#include "robot/comm_interface/factory/comm_factory.h"

namespace robot::comm_interface {

CommFactory::~CommFactory() {
    // The unique_ptrs in port_resources_ will automatically clean up
    // their respective PortResources, which includes joining the threads.
}

std::shared_ptr<Serial> CommFactory::GetSerial(const robot::comm_interface::SerialConfig& config) {
    const std::string& port = config.port();
    uint32_t baudrate = config.baudrate();

    std::lock_guard<std::mutex> lock(mutex_);

    auto& port_res_ptr = port_resources_[port];
    if (!port_res_ptr) {
        port_res_ptr = std::make_unique<PortResources>();
    }

    auto& serials = port_res_ptr->serials;
    auto it = serials.find(baudrate);
    if (it != serials.end()) {
        return it->second;
    }

    auto serial = std::make_shared<Serial>(port_res_ptr->io_context, port, baudrate);
    serials[baudrate] = serial;
    return serial;
}

} // namespace robot::comm_interface 