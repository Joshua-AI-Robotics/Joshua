#define PYBIND11_NO_ASSERT_GIL_HELD_INCREF_DECREF
#include "robot/nexus/ai_executor.h"
#include <glog/logging.h>
#include <pybind11/pybind11.h>
#include <thread>
#include <chrono>

namespace py = pybind11;

namespace robot::nexus {

struct AIExecutor::PybindData {
    pybind11::scoped_interpreter guard;
    std::string module_name;
    std::string function_name;
    PyThreadState* main_thread_state;
};

AIExecutor::AIExecutor() : pybind_data_(std::make_unique<PybindData>()) {
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

bool AIExecutor::Init(const std::string& module_name, const std::string& function_name) {
    LOG(INFO) << "AIExecutor::Init called on thread: " << std::this_thread::get_id();
    try {
        py::gil_scoped_acquire acquire;
        
        // Test that we can import the module and function
        auto model_module = py::module::import(module_name.c_str());
        auto model_func = model_module.attr(function_name.c_str());
        
        // Store names for later use
        pybind_data_->module_name = module_name;
        pybind_data_->function_name = function_name;
        
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
        auto start_time = std::chrono::steady_clock::now();
        
        py::gil_scoped_acquire acquire;
        
        auto end_time = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
        
        auto model_module = py::module::import(pybind_data_->module_name.c_str());
        auto model_func = model_module.attr(pybind_data_->function_name.c_str());
        
        py::bytes py_input(serialized_input);
        auto result = model_func(py_input);
        
        std::string serialized_output = result.cast<std::string>();
        
        if (!output_packet.ParseFromString(serialized_output)) {
            LOG(ERROR) << "Failed to parse NexusModelOutputPacket from Python.";
        }
        
    } catch (py::error_already_set &e) {
        LOG(ERROR) << "Python error in AIExecutor::Predict: " << e.what();
    }

    return output_packet;
}

} // namespace robot::nexus 