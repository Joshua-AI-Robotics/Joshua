#define PYBIND11_NO_ASSERT_GIL_HELD_INCREF_DECREF
#include "robot/nexus/ai_executor.h"
#include "robot/nexus/proto/nexus_packet.pb.h"
#include "config/proto/config.pb.h"
#include <glog/logging.h>
#include <pybind11/embed.h>
#include <pybind11/pybind11.h>
#include <thread>
#include <chrono>
#include <unordered_map>
#include <memory>

namespace py = pybind11;

namespace robot::nexus {

namespace {
    // Defined in ai_layer_gateway.py.
    constexpr auto kModuleName = "ai.ai_layer_gateway";
    constexpr auto kClassName = "AILayerGateway";
    constexpr auto kInferenceMethodName = "get_action";
    constexpr auto kStoreDatasetMethodName = "store_as_lerobot_dataset";
}

struct AIExecutor::PybindData {
    pybind11::scoped_interpreter guard;
    PyThreadState* main_thread_state;
    py::object gateway_instance;
};

AIExecutor::AIExecutor(const config::Config& config) : config_(config), pybind_data_(std::make_unique<PybindData>()) {
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
        if (!config_.SerializeToString(&serialized_config)) {
            LOG(ERROR) << "Failed to serialize Config protobuf.";
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
        
        // Call the inference method on the stored class instance.
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

void AIExecutor::StoreAsLeRobotDataset(const NexusModelInputPacket& input_packet,
                                       const NexusModelOutputPacket& output_packet,
                                       int episode_index,
                                       bool is_last_step) {
    std::string serialized_input, serialized_output;
    
    if (!input_packet.SerializeToString(&serialized_input)) {
        LOG(ERROR) << "Failed to serialize input packet for dataset storage.";
        return;
    }
    
    if (!output_packet.SerializeToString(&serialized_output)) {
        LOG(ERROR) << "Failed to serialize output packet for dataset storage.";
        return;
    }

    try {        
        py::gil_scoped_acquire acquire;
        
        py::bytes py_input(serialized_input);
        py::bytes py_output(serialized_output);
        py::int_ py_episode(episode_index);
        py::bool_ py_is_last(is_last_step);
        
        // Call the dataset storage method
        pybind_data_->gateway_instance.attr(kStoreDatasetMethodName)(py_input, py_output, py_episode, py_is_last);
        
    } catch (py::error_already_set &e) {
        LOG(ERROR) << "Python error in AIExecutor::StoreAsLeRobotDataset: " << e.what();
    }
}

void AIExecutor::SaveDataset(const std::string& output_dir) {
    try {        
        py::gil_scoped_acquire acquire;
        
        py::str py_output_dir(output_dir);
        
        // Call the save dataset method
        pybind_data_->gateway_instance.attr("save_dataset")(py_output_dir);
        
    } catch (py::error_already_set &e) {
        LOG(ERROR) << "Python error in AIExecutor::SaveDataset: " << e.what();
    }
}

} // namespace robot::nexus 