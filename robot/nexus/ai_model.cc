#define PYBIND11_NO_ASSERT_GIL_HELD_INCREF_DECREF
#include "robot/nexus/ai_model.h"
#include <glog/logging.h>
#include <pybind11/pybind11.h>

namespace py = pybind11;

namespace robot::nexus {

struct AIModel::PybindData {
    pybind11::scoped_interpreter guard;
    std::string module_name;
    std::string function_name;
};

AIModel::AIModel() : pybind_data_(std::make_unique<PybindData>()) {}

AIModel::~AIModel() = default;

bool AIModel::Init(const std::string& module_name, const std::string& function_name) {
    try {
        py::gil_scoped_acquire acquire;
        // Test that we can import the module and function
        auto model_module = py::module::import(module_name.c_str());
        auto model_func = model_module.attr(function_name.c_str());
        
        // Store names for later use
        pybind_data_->module_name = module_name;
        pybind_data_->function_name = function_name;
    } catch (py::error_already_set &e) {
        LOG(ERROR) << "Failed to initialize AI model: " << e.what();
        return false;
    }
    return true;
}

NexusModelOutputPacket AIModel::Predict(const NexusModelInputPacket& input_packet) {
    LOG(INFO) << "DEBUG: AIModel::Predict called";
    NexusModelOutputPacket output_packet;
    std::string serialized_input;
    
    LOG(INFO) << "DEBUG: Serializing input packet...";
    if (!input_packet.SerializeToString(&serialized_input)) {
        LOG(ERROR) << "Failed to serialize NexusModelInputPacket.";
        return output_packet; // Return empty packet on failure
    }
    LOG(INFO) << "DEBUG: Input packet serialized, size: " << serialized_input.size();

    try {
        LOG(INFO) << "DEBUG: Acquiring GIL...";
        py::gil_scoped_acquire acquire;
        
        LOG(INFO) << "DEBUG: GIL acquired, importing module...";
        auto model_module = py::module::import(pybind_data_->module_name.c_str());
        auto model_func = model_module.attr(pybind_data_->function_name.c_str());
        
        LOG(INFO) << "DEBUG: Creating py::bytes...";
        py::bytes py_input(serialized_input);
        LOG(INFO) << "DEBUG: Calling Python function...";
        auto result = model_func(py_input);
        LOG(INFO) << "DEBUG: Python function returned, casting result...";
        
        std::string serialized_output = result.cast<std::string>();
        LOG(INFO) << "DEBUG: Result cast complete, size: " << serialized_output.size();
        
        LOG(INFO) << "DEBUG: Parsing output packet...";
        if (!output_packet.ParseFromString(serialized_output)) {
            LOG(ERROR) << "Failed to parse NexusModelOutputPacket from Python.";
        } else {
            LOG(INFO) << "DEBUG: Output packet parsed successfully";
        }
        
        LOG(INFO) << "DEBUG: GIL released automatically";
    } catch (py::error_already_set &e) {
        LOG(ERROR) << "Python error in AIModel::Predict: " << e.what();
    }

    LOG(INFO) << "DEBUG: AIModel::Predict returning";
    return output_packet;
}

} // namespace robot::nexus 