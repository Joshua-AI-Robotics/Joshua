#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include "google/protobuf/descriptor.h"
#include "google/protobuf/reflection.h"
#include "google/protobuf/message.h"
#include "config/proto/config.pb.h"

namespace config_gui {

// Structure to hold field information
struct FieldInfo {
    std::string name;
    std::string display_name;
    std::string value;
    std::string field_type;
    bool is_enum;
    std::vector<std::string> enum_values;
    bool is_repeated;
    bool is_message;
    bool is_oneof;
    std::string oneof_name;
};

// Structure to hold message information
struct MessageInfo {
    std::string name;
    std::string display_name;
    std::vector<FieldInfo> fields;
    std::vector<MessageInfo> sub_messages;
};

class ProtoParser {
public:
    ProtoParser();
    ~ProtoParser() = default;

    // Parse the main config message
    MessageInfo parseConfigMessage(const config::Config& config);
    
    // Parse any protobuf message dynamically
    MessageInfo parseMessage(const google::protobuf::Message& message);
    
    // Get enum values for a field
    std::vector<std::string> getEnumValues(const google::protobuf::FieldDescriptor* field);
    
    // Get field value as string
    std::string getFieldValueAsString(const google::protobuf::Message& message, 
                                     const google::protobuf::FieldDescriptor* field);
    
    // Set field value from string
    bool setFieldValueFromString(google::protobuf::Message* message,
                                const google::protobuf::FieldDescriptor* field,
                                const std::string& value);
    
    // Get repeated field count
    int getRepeatedFieldCount(const google::protobuf::Message& message,
                             const google::protobuf::FieldDescriptor* field);
    
    // Add item to repeated field
    bool addRepeatedFieldItem(google::protobuf::Message* message,
                             const google::protobuf::FieldDescriptor* field);
    
    // Remove last item from repeated field
    bool removeLastRepeatedFieldItem(google::protobuf::Message* message,
                                    const google::protobuf::FieldDescriptor* field);
    
    // Get repeated field item
    google::protobuf::Message* getRepeatedFieldItem(google::protobuf::Message* message,
                                                   const google::protobuf::FieldDescriptor* field,
                                                   int index);
    
    // Get oneof field name
    std::string getOneofFieldName(const google::protobuf::Message& message,
                                 const google::protobuf::FieldDescriptor* field);
    
    // Set oneof field
    bool setOneofField(google::protobuf::Message* message,
                      const std::string& oneof_name,
                      const std::string& field_name);
    
    // Get display name for a field
    std::string getDisplayName(const std::string& field_name);

private:
    // Helper methods
    std::string getFieldTypeName(const google::protobuf::FieldDescriptor* field);
    bool isRepeatedMessage(const google::protobuf::FieldDescriptor* field);
    void parseMessageFields(const google::protobuf::Message& message, 
                           MessageInfo& message_info);
};

} // namespace config_gui 