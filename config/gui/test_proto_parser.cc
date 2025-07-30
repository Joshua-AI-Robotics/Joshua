#include <iostream>
#include <memory>
#include "config/gui/proto_parser.h"
#include "config/proto/config.pb.h"

int main() {
    std::cout << "Testing Dynamic Proto Parser..." << std::endl;
    
    // Create a proto parser
    auto parser = std::make_shared<config_gui::ProtoParser>();
    
    // Create a sample config
    config::Config config;
    config.set_operation_mode(config::MODE_INFERENCE);
    
    auto* robot = config.mutable_robot();
    robot->set_name("test_robot");
    robot->set_id(123);
    robot->set_robot_type(config::MANIPULATOR_ARM);
    
    auto* ai = config.mutable_ai();
    ai->set_policy_name("test_policy");
    ai->set_pretrained_model_path("/path/to/test/model");
    ai->set_act_dim(6);
    ai->set_state_dim(18);
    
    // Parse the config
    config_gui::MessageInfo config_info = parser->parseConfigMessage(config);
    
    std::cout << "\n=== Parsed Config Message ===" << std::endl;
    std::cout << "Name: " << config_info.name << std::endl;
    std::cout << "Display Name: " << config_info.display_name << std::endl;
    std::cout << "Number of fields: " << config_info.fields.size() << std::endl;
    
    std::cout << "\n=== Fields ===" << std::endl;
    for (const auto& field : config_info.fields) {
        std::cout << "Field: " << field.name << std::endl;
        std::cout << "  Display Name: " << field.display_name << std::endl;
        std::cout << "  Type: " << field.field_type << std::endl;
        std::cout << "  Value: " << field.value << std::endl;
        std::cout << "  Is Enum: " << (field.is_enum ? "Yes" : "No") << std::endl;
        std::cout << "  Is Repeated: " << (field.is_repeated ? "Yes" : "No") << std::endl;
        std::cout << "  Is Message: " << (field.is_message ? "Yes" : "No") << std::endl;
        std::cout << "  Is Oneof: " << (field.is_oneof ? "Yes" : "No") << std::endl;
        
        if (field.is_enum) {
            std::cout << "  Enum Values: ";
            for (const auto& enum_value : field.enum_values) {
                std::cout << enum_value << " ";
            }
            std::cout << std::endl;
        }
        
        if (field.is_oneof) {
            std::cout << "  Oneof Name: " << field.oneof_name << std::endl;
        }
        
        std::cout << std::endl;
    }
    
    // Test parsing individual messages
    std::cout << "\n=== Testing Individual Message Parsing ===" << std::endl;
    
    // Parse robot message
    config_gui::MessageInfo robot_info = parser->parseMessage(*robot);
    std::cout << "Robot Message:" << std::endl;
    std::cout << "  Name: " << robot_info.name << std::endl;
    std::cout << "  Display Name: " << robot_info.display_name << std::endl;
    std::cout << "  Fields: " << robot_info.fields.size() << std::endl;
    
    for (const auto& field : robot_info.fields) {
        std::cout << "    " << field.display_name << " (" << field.field_type << "): " << field.value << std::endl;
    }
    
    // Parse AI message
    config_gui::MessageInfo ai_info = parser->parseMessage(*ai);
    std::cout << "\nAI Message:" << std::endl;
    std::cout << "  Name: " << ai_info.name << std::endl;
    std::cout << "  Display Name: " << ai_info.display_name << std::endl;
    std::cout << "  Fields: " << ai_info.fields.size() << std::endl;
    
    for (const auto& field : ai_info.fields) {
        std::cout << "    " << field.display_name << " (" << field.field_type << "): " << field.value << std::endl;
    }
    
    std::cout << "\nDynamic parsing test completed successfully!" << std::endl;
    
    return 0;
} 