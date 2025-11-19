#pragma once

#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <streambuf>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "config/proto/config.pb.h"
#include "glog/logging.h"
#include "google/protobuf/text_format.h"

namespace config::config_util {

/// @brief Extracts the base config path from config content.
/// @param config_content The config file content.
/// @param config_dir The directory of the current config file.
/// @return Path to base config if found, empty string otherwise.
inline std::string ExtractBaseConfig(const std::string& config_content, 
                                     const std::string& config_dir) {
  // Look for pattern: base: "filename.pbtxt"
  size_t base_pos = config_content.find("base:");
  if (base_pos == std::string::npos) {
    return "";
  }
  
  // Find the opening quote
  size_t quote_start = config_content.find("\"", base_pos);
  if (quote_start == std::string::npos) {
    return "";
  }
  
  // Find the closing quote
  size_t quote_end = config_content.find("\"", quote_start + 1);
  if (quote_end == std::string::npos) {
    return "";
  }
  
  std::string base_filename = config_content.substr(quote_start + 1, 
                                                     quote_end - quote_start - 1);
  
  // Construct full path relative to config directory
  return config_dir + "/" + base_filename;
}

/// @brief Removes the base: line from config content.
/// @param config_content The config file content.
/// @return Config content with base: line removed.
inline std::string RemoveBaseLine(const std::string& config_content) {
  size_t base_pos = config_content.find("base:");
  if (base_pos == std::string::npos) {
    return config_content;
  }
  
  // Find the end of the line
  size_t line_end = config_content.find("\n", base_pos);
  if (line_end == std::string::npos) {
    // No newline found, remove from base_pos to end
    return config_content.substr(0, base_pos);
  }
  
  // Remove the line including the newline
  std::string result = config_content.substr(0, base_pos);
  if (line_end + 1 < config_content.length()) {
    result += config_content.substr(line_end + 1);
  }
  
  return result;
}

/// @brief Loads a config::Config from a text-format protobuf file with base config support.
/// @param config_path Path to the config file.
/// @return Parsed config::Config object.
/// @throws std::runtime_error if file cannot be opened or parsed.
inline absl::StatusOr<config::Config> LoadConfig(const std::string& config_path) {
  config::Config config;
  
  // Read the config file
  std::ifstream input(config_path);
  if (!input) {
    LOG(ERROR) << "Failed to open config file: " << config_path;
    return absl::Status(absl::StatusCode::kInvalidArgument,
                        "Failed to open config file: " + config_path);
  }
  std::string config_content((std::istreambuf_iterator<char>(input)),
                             std::istreambuf_iterator<char>());
  input.close();
  
  // Extract config directory for relative base path resolution
  size_t last_slash = config_path.find_last_of("/\\");
  std::string config_dir = (last_slash != std::string::npos) 
                           ? config_path.substr(0, last_slash) 
                           : ".";
  
  // Check for base config
  std::string base_config_path = ExtractBaseConfig(config_content, config_dir);
  if (!base_config_path.empty()) {
    LOG(INFO) << "Loading base config: " << base_config_path;
    
    // Load base config recursively (supports nested base configs)
    auto base_result = LoadConfig(base_config_path);
    if (!base_result.ok()) {
      LOG(ERROR) << "Failed to load base config: " << base_config_path;
      return base_result;
    }
    config = *base_result;
    
    // Remove base: line from current config content
    config_content = RemoveBaseLine(config_content);
  }
  
  // Parse current config and merge with base
  config::Config override_config;
  if (!google::protobuf::TextFormat::ParseFromString(config_content, &override_config)) {
    LOG(ERROR) << "Failed to parse config from file: " << config_path;
    return absl::Status(absl::StatusCode::kInvalidArgument,
                        "Failed to parse config from file: " + config_path);
  }
  
  // Merge override config into base config
  config.MergeFrom(override_config);
  
  return config;
}

/// @brief Represents a protobuf field, including nested fields for message types.
struct ProtoField {
  const google::protobuf::FieldDescriptor* descriptor;
  std::vector<ProtoField> nested_fields;

  /// @brief Constructs a ProtoField from a FieldDescriptor, populating nested fields if message
  /// type.
  explicit ProtoField(const google::protobuf::FieldDescriptor* d) : descriptor(d) {
    if (d) {
      populate_nested_fields();
    }
  }

  bool is_repeated() const {
    return descriptor && descriptor->is_repeated();
  }

  bool is_message() const {
    return descriptor &&
           descriptor->cpp_type() == google::protobuf::FieldDescriptor::CPPTYPE_MESSAGE;
  }

  std::string name() const {
    return descriptor ? descriptor->name() : "";
  }

  std::string type() const {
    return descriptor ? descriptor->type_name() : "";
  }

  int number() const {
    return descriptor ? descriptor->number() : -1;
  }

  bool is_required() const {
    return descriptor && descriptor->is_required();
  }

  bool is_optional() const {
    return descriptor && descriptor->is_optional();
  }

  std::string default_value() const {
    if (!descriptor) return "";

    switch (descriptor->cpp_type()) {
      case google::protobuf::FieldDescriptor::CPPTYPE_INT32:
        return std::to_string(descriptor->default_value_int32());
      case google::protobuf::FieldDescriptor::CPPTYPE_INT64:
        return std::to_string(descriptor->default_value_int64());
      case google::protobuf::FieldDescriptor::CPPTYPE_UINT32:
        return std::to_string(descriptor->default_value_uint32());
      case google::protobuf::FieldDescriptor::CPPTYPE_UINT64:
        return std::to_string(descriptor->default_value_uint64());
      case google::protobuf::FieldDescriptor::CPPTYPE_DOUBLE:
        return std::to_string(descriptor->default_value_double());
      case google::protobuf::FieldDescriptor::CPPTYPE_FLOAT:
        return std::to_string(descriptor->default_value_float());
      case google::protobuf::FieldDescriptor::CPPTYPE_BOOL:
        return descriptor->default_value_bool() ? "true" : "false";
      case google::protobuf::FieldDescriptor::CPPTYPE_ENUM:
        return descriptor->default_value_enum()->name();
      case google::protobuf::FieldDescriptor::CPPTYPE_STRING:
        return descriptor->default_value_string();
      default:
        return "";
    }
  }

 private:
  void populate_nested_fields() {
    if (!is_message()) {
      return;
    }
    const google::protobuf::Descriptor* message_descriptor = descriptor->message_type();
    if (!message_descriptor) {
      return;
    }
    for (int i = 0; i < message_descriptor->field_count(); ++i) {
      const google::protobuf::FieldDescriptor* nested_field = message_descriptor->field(i);
      nested_fields.emplace_back(nested_field);
    }
  }
};

/**
 * @brief Returns a vector of ProtoField for all fields in the given descriptor.
 * @param descriptor The protobuf descriptor.
 * @return Vector of ProtoField objects.
 */
inline std::vector<ProtoField> GetProtoFields(const google::protobuf::Descriptor* descriptor) {
  std::vector<ProtoField> fields;
  if (!descriptor) {
    return fields;
  }
  for (int i = 0; i < descriptor->field_count(); ++i) {
    const google::protobuf::FieldDescriptor* field = descriptor->field(i);
    fields.emplace_back(field);
  }
  return fields;
}

/**
 * @brief Get all fields from a protobuf message.
 * @param message The protobuf message.
 * @return Vector of ProtoField objects.
 */
inline std::vector<ProtoField> GetProtoFields(const google::protobuf::Message& message) {
  return GetProtoFields(message.GetDescriptor());
}

/**
 * @brief Prints the fields using a callback, with indentation for nested fields.
 */
inline void PrintFields(const std::vector<ProtoField>& fields,
                        std::function<void(const std::string&)> print_callback,
                        int indent = 0) {
  std::string indent_str(indent * 2, ' ');
  for (const auto& field : fields) {
    std::string field_info = indent_str + field.name() + " (" + field.type() + ")";
    if (field.is_repeated()) {
      field_info += " [repeated]";
    }
    if (field.is_required()) {
      field_info += " [required]";
    }
    if (!field.default_value().empty()) {
      field_info += " [default: " + field.default_value() + "]";
    }
    print_callback(field_info);
    if (!field.nested_fields.empty()) {
      PrintFields(field.nested_fields, print_callback, indent + 1);
    }
  }
}

}  // namespace config::config_util
