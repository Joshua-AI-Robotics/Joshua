// Pure setup inspection. Factory descriptor queries never invoke their creators.
#include "config/inspection_cli.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "config/config_validation_cli.h"
#include "config/validation.h"
#include "google/protobuf/struct.pb.h"
#include "google/protobuf/util/json_util.h"
#include "robot/action/factory/action_factory.h"
#include "robot/board/factory/board_factory.h"
#include "robot/comm/factory/comm_factory.h"
#include "robot/perception/factory/perception_factory.h"
#include "version.h"

using google::protobuf::Struct;
using google::protobuf::Value;
namespace {
Value Text(const std::string& text) {
  Value v;
  v.set_string_value(text);
  return v;
}
Value Number(double number) {
  Value v;
  v.set_number_value(number);
  return v;
}
Value Object() {
  Value v;
  v.mutable_struct_value();
  return v;
}
Value Null() {
  Value v;
  v.set_null_value(google::protobuf::NULL_VALUE);
  return v;
}
Value Array() {
  Value v;
  v.mutable_list_value();
  return v;
}
void Set(Value& v, const std::string& key, Value value) {
  (*v.mutable_struct_value()->mutable_fields())[key] = std::move(value);
}
Value Proto(const google::protobuf::Message& message) {
  std::string json;
  google::protobuf::util::JsonPrintOptions options;
  options.preserve_proto_field_names = true;
  auto status = google::protobuf::util::MessageToJsonString(message, &json, options);
  if (!status.ok()) throw std::runtime_error("Cannot serialize config.");
  Value value;
  if (!google::protobuf::util::JsonStringToMessage(json, &value).ok())
    throw std::runtime_error("Cannot encode config.");
  return value;
}
Value Entry(const std::string& name, const std::string& scope) {
  Value v = Object();
  Set(v, "name", Text(name));
  Set(v, "support_scope", Text(scope));
  return v;
}
Value Catalog() {
  Value data = Object(), boards = Array(), comm = Array(), motors = Array(), sensors = Array();
  for (const auto& e : robot::board::BoardFactory::Implementations())
    *boards.mutable_list_value()->add_values() =
        Entry(robot::board::BoardType_Name(e.type), e.support_scope);
  for (const auto& e : robot::comm::CommFactory::Implementations()) {
    auto v = Entry(robot::comm::CommType_Name(e.mechanism), e.support_scope);
    Set(v, "transport", Text(robot::comm::TransportType_Name(e.transport)));
    Set(v, "configuration_constraints", Text(e.configuration_constraints));
    *comm.mutable_list_value()->add_values() = v;
  }
  for (const auto& e : robot::action::ActionFactory::Implementations())
    *motors.mutable_list_value()->add_values() =
        Entry(robot::action::MotorType_Name(e.type), e.support_scope);
  for (const auto& e : robot::perception::PerceptionFactory::Implementations()) {
    auto v = Entry(robot::perception::SensorType_Name(e.type), e.support_scope);
    const auto* field =
        robot::perception::SinglePerception::descriptor()->FindFieldByNumber(e.config_case);
    Set(v, "sensor_config", Text(field ? field->name() : "none"));
    *sensors.mutable_list_value()->add_values() = v;
  }
  Set(data, "boards", boards);
  Set(data, "communication", comm);
  Set(data, "motors", motors);
  Set(data, "sensors", sensors);
  Set(data, "build_version", Text(JOSHUA_VERSION));
  Set(data,
      "support_evidence",
      Text("Fixed subsystem-owned factory descriptor and creator tables in this build; creators "
           "were not called."));
  Set(data, "integration_guide", Text("docs/AI_INTEGRATION.md"));
  return data;
}
Value Devices(const config::Config& cfg) {
  Value result = Object(), devices = Array();
  size_t total = 0;
  auto add = [&](const std::string& kind,
                 const std::string& name,
                 const google::protobuf::Message& config) {
    ++total;
    if (devices.list_value().values_size() >= 128 || kind.size() + 1 + name.size() > 512) return;
    Value v = Object();
    Set(v, "device_id", Text(kind + ":" + name));
    Set(v, "kind", Text(kind));
    Set(v, "name", Text(name));
    Set(v, "configured", Proto(config));
    Set(v, "hardware_readiness", Text("unknown"));
    Set(v, "compatibility", Text("not_verified_by_inspection"));
    *devices.mutable_list_value()->add_values() = std::move(v);
  };
  for (const auto& b : cfg.robot().boards()) add("board", b.name(), b);
  for (const auto& a : cfg.robot().actions().single_actions())
    add("actuator", a.actuator().actuator_name(), a);
  for (const auto& s : cfg.robot().perceptions().single_perceptions())
    add("sensor", s.sensor_name(), s);
  Set(result, "devices", devices);
  Set(result, "omitted_devices", Number(total - devices.list_value().values_size()));
  Set(result, "build_version", Text(JOSHUA_VERSION));
  Set(result, "config_name", Text(cfg.general().name()));
  return result;
}
int Run(const std::vector<std::string>& args, std::ostream& output) {
  std::string op, path, id;
  int root = -1;
  bool bad = false;
  for (size_t i = 0; i < args.size(); ++i) {
    std::string arg(args[i]);
    if (i + 1 >= args.size()) {
      bad = true;
      break;
    }
    std::string value(args[++i]);
    if (arg == "--operation" && op.empty())
      op = value;
    else if (arg == "--config" && path.empty())
      path = value;
    else if (arg == "--device-id" && id.empty())
      id = value;
    else if (arg == "--config-root-fd" && root == -1 && !value.empty() &&
             value.find_first_not_of("0123456789") == std::string::npos && value.size() < 10)
      root = std::stoi(value);
    else
      bad = true;
  }
  Value report = Object(), data = Object();
  Set(report, "schema_version", Number(1));
  Set(report, "scope", Text("setup_inspection"));
  Set(report, "operation", Text(op));
  Set(report, "config_path", path.empty() ? Null() : Text(path));
  Set(report, "stage", Text("arguments"));
  std::string code = "OK", message = "Inspection completed.";
  int exit = 0;
  if (bad ||
      (op != "list_supported_components" && op != "list_devices" && op != "describe_device") ||
      (op == "list_supported_components"
           ? (!path.empty() || !id.empty())
           : (root < 0 || path.empty() || (op == "describe_device" ? id.empty() : !id.empty())))) {
    code = "INVALID_ARGUMENT";
    message = "Require --operation and confined config reference (except config-free catalog).";
    exit = 2;
  } else if (op == "list_supported_components") {
    data = Catalog();
    Set(report, "stage", Text("inspection"));
  } else {
    std::string stage;
    auto loaded = config::LoadConfigForInspection(path, root, &stage);
    Set(report, "stage", Text(stage));
    if (!loaded.ok()) {
      code = absl::StatusCodeToString(loaded.status().code());
      message = std::string(loaded.status().message());
      exit = 1;
    } else {
      auto valid = config::ValidateConfig(*loaded);
      Set(report, "stage", Text("integrity"));
      if (!valid.ok()) {
        code = absl::StatusCodeToString(valid.code());
        message = std::string(valid.message());
        exit = 1;
      } else {
        data = Devices(*loaded);
        Set(report, "stage", Text("inspection"));
        if (op == "describe_device") {
          Value found;
          int matches = 0;
          auto consider = [&](const std::string& kind,
                              const std::string& name,
                              const google::protobuf::Message& cfg) {
            if (kind + ":" + name != id) return;
            ++matches;
            found = Object();
            Set(found, "device_id", Text(id));
            Set(found, "kind", Text(kind));
            Set(found, "name", Text(name));
            Set(found, "configured", Proto(cfg));
            Set(found, "hardware_readiness", Text("unknown"));
            Set(found, "compatibility", Text("not_verified_by_inspection"));
          };
          for (const auto& b : loaded->robot().boards()) consider("board", b.name(), b);
          for (const auto& a : loaded->robot().actions().single_actions())
            consider("actuator", a.actuator().actuator_name(), a);
          for (const auto& sensor : loaded->robot().perceptions().single_perceptions())
            consider("sensor", sensor.sensor_name(), sensor);
          if (matches != 1) {
            code = matches ? "FAILED_PRECONDITION" : "NOT_FOUND";
            message = "Device identity is absent or ambiguous in the complete candidate config.";
            exit = 1;
            data = Object();
          } else {
            data = Object();
            Set(data, "device", found);
          }
        }
      }
    }
  }
  if (message.size() > 8170) message = message.substr(0, 8170) + "... [truncated]";
  for (char& c : message)
    if (static_cast<unsigned char>(c) >= 127 || static_cast<unsigned char>(c) < 32) c = '?';
  Set(report, "status_code", Text(code));
  Set(report, "message", Text(message));
  Set(report, "data", data);
  Value limits = Array();
  *limits.mutable_list_value()->add_values() = Text(
      "Installed software selection is not physical compatibility or hardware readiness. No "
      "creator, driver, cache, ROS or device calls were made. Inventory omits entries beyond 128 "
      "or identities beyond 512 UTF-8 bytes; describe searches the complete candidate.");
  Set(report, "limitations", limits);
  std::string json;
  if (!google::protobuf::util::MessageToJsonString(report, &json).ok() || json.size() + 1 > 65536) {
    Set(report, "data", Object());
    Set(report, "status_code", Text("RESOURCE_EXHAUSTED"));
    Set(report,
        "message",
        Text("Inspection output exceeds 64 KiB; reduce the candidate configuration."));
    google::protobuf::util::MessageToJsonString(report, &json).IgnoreError();
    exit = 1;
  }
  output << json << '\n';
  output.flush();
  return output.good() ? exit : 3;
}
}  // namespace
namespace config {
int RunInspectionCli(const std::vector<std::string>& args, std::ostream& output) {
  try {
    return Run(args, output);
  } catch (...) {
    return 3;
  }
}
}  // namespace config
