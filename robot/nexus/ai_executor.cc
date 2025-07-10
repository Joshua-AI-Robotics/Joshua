#define PYBIND11_NO_ASSERT_GIL_HELD_INCREF_DECREF
#include "robot/nexus/ai_executor.h"
#include <glog/logging.h>
#include <pybind11/embed.h>
#include <pybind11/pybind11.h>
#include <thread>
#include <chrono>
#include <unordered_map>

namespace py = pybind11;

namespace robot::nexus {

struct AIExecutor::PybindData {
    pybind11::scoped_interpreter guard;
    PyThreadState* main_thread_state;
    std::unordered_map<std::string, py::module> modules;
    std::unordered_map<std::string, py::object> functions;
};

AIExecutor::AIExecutor(const config::Ai& ai_config) : ai_config_(ai_config), pybind_data_(std::make_unique<PybindData>()) {
    LOG(INFO) << "AIExecutor constructor started";
    
    // The scoped_interpreter initializes Python and acquires the GIL
    // We need to save the thread state and release the GIL so other threads can use it
    pybind_data_->main_thread_state = PyEval_SaveThread();

    // TODO: Remove this hardcode and update config and add map or enum.
    module_name_ = "ai.ai_layer_gateway";
    function_name_ = "generate_mock_ai_output";
}

AIExecutor::~AIExecutor() {
    // Restore the thread state before destruction
    if (pybind_data_->main_thread_state) {
        PyEval_RestoreThread(pybind_data_->main_thread_state);
    }
}

// TODO: This only allows one model and one function to be loaded at a time.
// Fix this to store multiple models and functions.
bool AIExecutor::Init() {
    LOG(INFO) << "AIExecutor::Init called on thread: " << std::this_thread::get_id();
    try {
        py::gil_scoped_acquire acquire;

        pybind_data_->modules[module_name_] = py::module::import(module_name_.c_str());
        pybind_data_->functions[function_name_] = pybind_data_->modules[module_name_].attr(function_name_.c_str());
        
        LOG(INFO) << "AI executor initialized successfully";
    } catch (py::error_already_set &e) {
        LOG(ERROR) << "Failed to initialize AI executor: " << e.what();
        return false;
    }
    LOG(INFO) << "AIExecutor::Init completed, GIL released automatically";
    return true;
}

NexusModelOutputPacket AIExecutor::Inference(const NexusModelInputPacket& input_packet) {
    NexusModelOutputPacket output_packet;
    std::string serialized_input;
    
    if (!input_packet.SerializeToString(&serialized_input)) {
        LOG(ERROR) << "Failed to serialize NexusModelInputPacket.";
        return output_packet; // Return empty packet on failure
    }

    try {        
        py::gil_scoped_acquire acquire;
                
        py::bytes py_input(serialized_input);
        auto result = pybind_data_->functions[function_name_](py_input);
        
        std::string serialized_output = result.cast<std::string>();
        
        if (!output_packet.ParseFromString(serialized_output)) {
            LOG(ERROR) << "Failed to parse NexusModelOutputPacket from Python.";
        }
        
    } catch (py::error_already_set &e) {
        LOG(ERROR) << "Python error in AIExecutor::Inference: " << e.what();
    }

    return output_packet;
}

} // namespace robot::nexus 