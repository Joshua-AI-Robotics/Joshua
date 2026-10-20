#pragma once

#include <algorithm>
#include <array>
#include <fstream>
#include <functional>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <streambuf>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "config/proto/config.pb.h"
#include "glog/logging.h"
#include "google/protobuf/io/tokenizer.h"
#include "google/protobuf/text_format.h"

namespace config::config_util {

class BoundedParseErrorCollector final : public google::protobuf::io::ErrorCollector {
 public:
  void RecordError(int line,
                   google::protobuf::io::ColumnNumber column,
                   absl::string_view message) override {
    if (!error_.empty()) return;
    error_ = "line " + std::to_string(line + 1) + ", column " + std::to_string(column + 1) + ": ";
    constexpr size_t kDetailLimit = 4096;
    const size_t remaining = error_.size() < kDetailLimit ? kDetailLimit - error_.size() : 0;
    error_.append(message.data(), std::min(message.size(), remaining));
    if (message.size() > remaining) error_ += "... [truncated]";
  }

  void RecordWarning(int line,
                     google::protobuf::io::ColumnNumber column,
                     absl::string_view message) override {
    LOG(WARNING) << "Config parse warning at line " << line + 1 << ", column " << column + 1 << ": "
                 << std::string(message.substr(0, 4096));
  }

  const std::string& error() const {
    return error_;
  }

 private:
  std::string error_;
};

/// @brief Loads a config::Config from a text-format protobuf file.
/// @param config_path Path to the config file.
/// @return Parsed config or a status with a bounded diagnostic on malformed input.
inline absl::StatusOr<config::Config> LoadConfig(const std::string& config_path,
                                                 std::optional<size_t> byte_limit = std::nullopt) {
  config::Config config;
  std::ifstream input(config_path, std::ios::binary);
  if (!input) {
    LOG(ERROR) << "Failed to open config file.";
    return absl::InvalidArgumentError("Failed to open config file.");
  }
  std::string config_content;
  std::array<char, 8192> chunk;
  while (input) {
    size_t available = chunk.size();
    if (byte_limit) {
      if (config_content.size() > *byte_limit) {
        return absl::ResourceExhaustedError("Config exceeds the byte limit.");
      }
      const size_t remaining = *byte_limit - config_content.size();
      if (remaining < available) {
        available = remaining + 1;
      }
    }
    input.read(chunk.data(), available);
    config_content.append(chunk.data(), static_cast<size_t>(input.gcount()));
    if (byte_limit && config_content.size() > *byte_limit) {
      return absl::ResourceExhaustedError("Config exceeds the byte limit.");
    }
  }
  if (input.bad() || (input.fail() && !input.eof())) {
    return absl::InternalError("Failed to read config file.");
  }
  BoundedParseErrorCollector errors;
  google::protobuf::TextFormat::Parser parser;
  parser.RecordErrorsTo(&errors);
  if (!parser.ParseFromString(config_content, &config)) {
    LOG(ERROR) << "Failed to parse config file.";
    return absl::InvalidArgumentError(errors.error().empty() ? "Failed to parse config file."
                                                             : errors.error());
  }
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
