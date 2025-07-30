#include "config/gui/proto_parser.h"
#include <sstream>
#include <algorithm>
#include <cctype>

namespace config_gui {

ProtoParser::ProtoParser() = default;

MessageInfo ProtoParser::parseConfigMessage(const config::Config& config) {
    return parseMessage(config);
}

MessageInfo ProtoParser::parseMessage(const google::protobuf::Message& message) {
    MessageInfo message_info;
    const google::protobuf::Descriptor* descriptor = message.GetDescriptor();
    
    message_info.name = descriptor->name();
    message_info.display_name = getDisplayName(descriptor->name());
    
    parseMessageFields(message, message_info);
    
    return message_info;
}

void ProtoParser::parseMessageFields(const google::protobuf::Message& message, 
                                    MessageInfo& message_info) {
    const google::protobuf::Descriptor* descriptor = message.GetDescriptor();
    const google::protobuf::Reflection* reflection = message.GetReflection();
    
    for (int i = 0; i < descriptor->field_count(); ++i) {
        const google::protobuf::FieldDescriptor* field = descriptor->field(i);
        
        // Skip if field is not set (for optional fields)
        if (!reflection->HasField(message, field)) {
            continue;
        }
        
        FieldInfo field_info;
        field_info.name = field->name();
        field_info.display_name = getDisplayName(field->name());
        field_info.field_type = getFieldTypeName(field);
        field_info.is_enum = field->type() == google::protobuf::FieldDescriptor::TYPE_ENUM;
        field_info.is_repeated = field->is_repeated();
        field_info.is_message = field->type() == google::protobuf::FieldDescriptor::TYPE_MESSAGE;
        field_info.is_oneof = field->containing_oneof() != nullptr;
        
        if (field_info.is_oneof) {
            field_info.oneof_name = field->containing_oneof()->name();
        }
        
        if (field_info.is_enum) {
            field_info.enum_values = getEnumValues(field);
        }
        
        if (!field_info.is_repeated) {
            field_info.value = getFieldValueAsString(message, field);
        }
        
        message_info.fields.push_back(field_info);
    }
}

std::vector<std::string> ProtoParser::getEnumValues(const google::protobuf::FieldDescriptor* field) {
    std::vector<std::string> values;
    const google::protobuf::EnumDescriptor* enum_desc = field->enum_type();
    
    for (int i = 0; i < enum_desc->value_count(); ++i) {
        const google::protobuf::EnumValueDescriptor* value_desc = enum_desc->value(i);
        values.push_back(value_desc->name());
    }
    
    return values;
}

std::string ProtoParser::getFieldValueAsString(const google::protobuf::Message& message, 
                                              const google::protobuf::FieldDescriptor* field) {
    const google::protobuf::Reflection* reflection = message.GetReflection();
    
    switch (field->type()) {
        case google::protobuf::FieldDescriptor::TYPE_INT32:
            return std::to_string(reflection->GetInt32(message, field));
        case google::protobuf::FieldDescriptor::TYPE_INT64:
            return std::to_string(reflection->GetInt64(message, field));
        case google::protobuf::FieldDescriptor::TYPE_UINT32:
            return std::to_string(reflection->GetUInt32(message, field));
        case google::protobuf::FieldDescriptor::TYPE_UINT64:
            return std::to_string(reflection->GetUInt64(message, field));
        case google::protobuf::FieldDescriptor::TYPE_FLOAT:
            return std::to_string(reflection->GetFloat(message, field));
        case google::protobuf::FieldDescriptor::TYPE_DOUBLE:
            return std::to_string(reflection->GetDouble(message, field));
        case google::protobuf::FieldDescriptor::TYPE_BOOL:
            return reflection->GetBool(message, field) ? "true" : "false";
        case google::protobuf::FieldDescriptor::TYPE_STRING:
            return reflection->GetString(message, field);
        case google::protobuf::FieldDescriptor::TYPE_ENUM:
            return reflection->GetEnum(message, field)->name();
        case google::protobuf::FieldDescriptor::TYPE_MESSAGE:
            return "[Message]";
        default:
            return "[Unknown Type]";
    }
}

bool ProtoParser::setFieldValueFromString(google::protobuf::Message* message,
                                        const google::protobuf::FieldDescriptor* field,
                                        const std::string& value) {
    const google::protobuf::Reflection* reflection = message->GetReflection();
    
    try {
        switch (field->type()) {
            case google::protobuf::FieldDescriptor::TYPE_INT32:
                reflection->SetInt32(message, field, std::stoi(value));
                break;
            case google::protobuf::FieldDescriptor::TYPE_INT64:
                reflection->SetInt64(message, field, std::stol(value));
                break;
            case google::protobuf::FieldDescriptor::TYPE_UINT32:
                reflection->SetUInt32(message, field, std::stoul(value));
                break;
            case google::protobuf::FieldDescriptor::TYPE_UINT64:
                reflection->SetUInt64(message, field, std::stoull(value));
                break;
            case google::protobuf::FieldDescriptor::TYPE_FLOAT:
                reflection->SetFloat(message, field, std::stof(value));
                break;
            case google::protobuf::FieldDescriptor::TYPE_DOUBLE:
                reflection->SetDouble(message, field, std::stod(value));
                break;
            case google::protobuf::FieldDescriptor::TYPE_BOOL:
                reflection->SetBool(message, field, (value == "true" || value == "1"));
                break;
            case google::protobuf::FieldDescriptor::TYPE_STRING:
                reflection->SetString(message, field, value);
                break;
            case google::protobuf::FieldDescriptor::TYPE_ENUM:
                {
                    const google::protobuf::EnumDescriptor* enum_desc = field->enum_type();
                    const google::protobuf::EnumValueDescriptor* value_desc = enum_desc->FindValueByName(value);
                    if (value_desc) {
                        reflection->SetEnum(message, field, value_desc);
                    } else {
                        return false;
                    }
                }
                break;
            default:
                return false;
        }
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

int ProtoParser::getRepeatedFieldCount(const google::protobuf::Message& message,
                                     const google::protobuf::FieldDescriptor* field) {
    const google::protobuf::Reflection* reflection = message.GetReflection();
    return reflection->FieldSize(message, field);
}

bool ProtoParser::addRepeatedFieldItem(google::protobuf::Message* message,
                                     const google::protobuf::FieldDescriptor* field) {
    const google::protobuf::Reflection* reflection = message->GetReflection();
    
    if (field->type() == google::protobuf::FieldDescriptor::TYPE_MESSAGE) {
        reflection->AddMessage(message, field);
        return true;
    }
    return false;
}

bool ProtoParser::removeLastRepeatedFieldItem(google::protobuf::Message* message,
                                            const google::protobuf::FieldDescriptor* field) {
    const google::protobuf::Reflection* reflection = message->GetReflection();
    int size = reflection->FieldSize(*message, field);
    
    if (size > 0) {
        reflection->RemoveLast(message, field);
        return true;
    }
    return false;
}

google::protobuf::Message* ProtoParser::getRepeatedFieldItem(google::protobuf::Message* message,
                                                           const google::protobuf::FieldDescriptor* field,
                                                           int index) {
    const google::protobuf::Reflection* reflection = message->GetReflection();
    
    if (field->type() == google::protobuf::FieldDescriptor::TYPE_MESSAGE && 
        index < reflection->FieldSize(*message, field)) {
        return reflection->MutableRepeatedMessage(message, field, index);
    }
    return nullptr;
}

std::string ProtoParser::getOneofFieldName(const google::protobuf::Message& message,
                                         const google::protobuf::FieldDescriptor* field) {
    const google::protobuf::Reflection* reflection = message.GetReflection();
    const google::protobuf::OneofDescriptor* oneof = field->containing_oneof();
    
    if (oneof && reflection->HasOneof(message, oneof)) {
        const google::protobuf::FieldDescriptor* set_field = reflection->GetOneofFieldDescriptor(message, oneof);
        if (set_field) {
            return set_field->name();
        }
    }
    return "";
}

bool ProtoParser::setOneofField(google::protobuf::Message* message,
                              const std::string& oneof_name,
                              const std::string& field_name) {
    const google::protobuf::Descriptor* descriptor = message->GetDescriptor();
    const google::protobuf::OneofDescriptor* oneof = descriptor->FindOneofByName(oneof_name);
    
    if (!oneof) {
        return false;
    }
    
    const google::protobuf::FieldDescriptor* field = descriptor->FindFieldByName(field_name);
    if (!field || field->containing_oneof() != oneof) {
        return false;
    }
    
    const google::protobuf::Reflection* reflection = message->GetReflection();
    reflection->ClearOneof(message, oneof);
    
    if (field->type() == google::protobuf::FieldDescriptor::TYPE_MESSAGE) {
        reflection->MutableMessage(message, field);
    }
    
    return true;
}

std::string ProtoParser::getDisplayName(const std::string& field_name) {
    std::string display_name = field_name;
    
    // Convert snake_case to Title Case
    bool capitalize_next = true;
    for (char& c : display_name) {
        if (c == '_') {
            c = ' ';
            capitalize_next = true;
        } else if (capitalize_next) {
            c = std::toupper(c);
            capitalize_next = false;
        }
    }
    
    return display_name;
}

std::string ProtoParser::getFieldTypeName(const google::protobuf::FieldDescriptor* field) {
    switch (field->type()) {
        case google::protobuf::FieldDescriptor::TYPE_INT32:
            return "int32";
        case google::protobuf::FieldDescriptor::TYPE_INT64:
            return "int64";
        case google::protobuf::FieldDescriptor::TYPE_UINT32:
            return "uint32";
        case google::protobuf::FieldDescriptor::TYPE_UINT64:
            return "uint64";
        case google::protobuf::FieldDescriptor::TYPE_FLOAT:
            return "float";
        case google::protobuf::FieldDescriptor::TYPE_DOUBLE:
            return "double";
        case google::protobuf::FieldDescriptor::TYPE_BOOL:
            return "bool";
        case google::protobuf::FieldDescriptor::TYPE_STRING:
            return "string";
        case google::protobuf::FieldDescriptor::TYPE_ENUM:
            return field->enum_type()->name();
        case google::protobuf::FieldDescriptor::TYPE_MESSAGE:
            return field->message_type()->name();
        default:
            return "unknown";
    }
}

bool ProtoParser::isRepeatedMessage(const google::protobuf::FieldDescriptor* field) {
    return field->is_repeated() && field->type() == google::protobuf::FieldDescriptor::TYPE_MESSAGE;
}

} // namespace config_gui 