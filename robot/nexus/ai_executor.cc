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

namespace {
    // Defined in ai_layer_gateway.py.
    constexpr auto kModuleName = "ai.ai_layer_gateway";
    constexpr auto kClassName = "AILayerGateway";
    constexpr auto kInferenceMethodName = "get_action";
}

struct AIExecutor::PybindData {
    pybind11::scoped_interpreter guard;
    PyThreadState* main_thread_state;
    py::object gateway_instance;
};

AIExecutor::AIExecutor(const config::Ai& ai_config) : ai_config_(ai_config), pybind_data_(std::make_unique<PybindData>()) {
    LOG(INFO) << "AIExecutor constructor started";
    
    // The scoped_interpreter initializes Python and acquires the GIL
    // We need to save the thread state and release the GIL so other threads can use it
    pybind_data_->main_thread_state = PyEval_SaveThread();
}

AIExecutor::~AIExecutor() {
    // Restore the thread state before destruction
    if (pybind_data_->main_thread_state) {
        PyEval_RestoreThread(pybind_data_->main_thread_state);
    }
}

bool AIExecutor::Init() {
    LOG(INFO) << "AIExecutor::Init called on thread: " << std::this_thread::get_id();
    try {
        py::gil_scoped_acquire acquire;

        // Import the module, get the class, and instantiate it.
        py::module module = py::module::import(kModuleName);
        py::object gateway_class = module.attr(kClassName);

        // Serialize the protobuf config to pass it to Python.
        // pybind11 cannot automatically cast custom C++ types like protobuf objects.
        std::string serialized_config;
        if (!ai_config_.SerializeToString(&serialized_config)) {
            LOG(ERROR) << "Failed to serialize Ai config protobuf.";
            return false;
        }
        
        pybind_data_->gateway_instance = gateway_class(py::bytes(serialized_config)); // This calls the Python __init__
        
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
        
        // Call the method on the stored class instance.
        auto result = pybind_data_->gateway_instance.attr(kInferenceMethodName)(py_input);
        
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