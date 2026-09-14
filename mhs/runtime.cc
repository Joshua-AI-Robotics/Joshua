#include "mhs/runtime.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <utility>

#include "absl/status/status.h"
#include "robot/board/factory/board_resolver.h"

namespace mhs {
namespace {
using google::protobuf::Struct;
using google::protobuf::Value;
Value V(const std::string& x) {
  Value v;
  v.set_string_value(x);
  return v;
}
Value V(const char* x) {
  return V(std::string(x));
}
Value V(double x) {
  Value v;
  v.set_number_value(x);
  return v;
}
Value V(bool x) {
  Value v;
  v.set_bool_value(x);
  return v;
}
Value V(const Struct& x) {
  Value v;
  *v.mutable_struct_value() = x;
  return v;
}
Struct Error(const std::string& message) {
  Struct out;
  (*out.mutable_fields())["error"] = V(message);
  return out;
}
bool Positive(double x) {
  return std::isfinite(x) && x > 0;
}
const Value& Field(const Struct& s, const std::string& name) {
  static const Value empty;
  auto it = s.fields().find(name);
  return it == s.fields().end() ? empty : it->second;
}
}  // namespace

absl::StatusOr<Device> ResolveDevice(const config::Config& config) {
  const auto& api = config.hardware_api();
  if (!api.enabled() || api.devices_size() != 1) {
    return absl::InvalidArgumentError(
        "MVP requires hardware_api.enabled and exactly one exposed device");
  }
  Device d;
  d.exposure = api.devices(0);
  int matches = 0;
  for (const auto& action : config.robot().actions().single_actions()) {
    if (action.has_actuator() && action.actuator().actuator_name() == d.exposure.actuator_name()) {
      d.actuator = action.actuator();
      ++matches;
    }
  }
  if (matches != 1 || d.exposure.actuator_name().empty() || d.exposure.description().empty()) {
    return absl::InvalidArgumentError(
        "Exposure must name exactly one actuator and provide a description");
  }
  const auto& a = d.actuator;
  auto resolved = robot::board::ResolveChannelConfig(
      config.robot().boards(), "hardware_api", a.board_name(), a.channel());
  if (!resolved.ok()) return resolved.status();
  d.board = *resolved->board;
  if (a.motor_type() != robot::action::MOTOR_STEPPER_NEMA17 || !a.has_stepper_config() ||
      d.board.board_type() != robot::board::TEENSY41 || d.board.channels_size() != 1 ||
      a.channel() != 0 || resolved->channel->drive() != robot::board::STEP_DIR ||
      !resolved->channel->has_step_dir()) {
    return absl::InvalidArgumentError(
        "MVP supports a single Teensy STEP_DIR channel 0 with a stepper motor");
  }
  d.steps_per_degree =
      static_cast<double>(a.stepper_config().steps_per_degree()) * a.stepper_config().gear_ratio();
  const double lo = a.operational_lower_limit(), hi = a.operational_upper_limit();
  // Exact integer positions must fit float32 feedback and int32 firmware counts.
  constexpr double kMaxExactSteps = 16777215;
  if (!Positive(a.stepper_config().steps_per_degree()) ||
      !Positive(a.stepper_config().gear_ratio()) || !Positive(d.steps_per_degree) ||
      !std::isfinite(lo) || !std::isfinite(hi) || lo >= hi ||
      !std::isfinite(a.physical_lower_limit()) || !std::isfinite(a.physical_upper_limit()) ||
      lo < a.physical_lower_limit() || hi > a.physical_upper_limit() ||
      std::max(std::abs(lo), std::abs(hi)) * d.steps_per_degree > kMaxExactSteps ||
      !Positive(d.exposure.max_move_degrees()) || d.exposure.max_move_duration_ms() == 0 ||
      d.exposure.max_move_duration_ms() > 60000 || d.exposure.feedback_max_age_ms() == 0 ||
      d.exposure.feedback_max_age_ms() > 1000) {
    return absl::InvalidArgumentError(
        "Invalid limits, conversion, move duration (1..60000 ms), or freshness (1..1000 ms)");
  }
  return d;
}

Runtime::Runtime(Device device) : device_(std::move(device)) {}
Runtime::~Runtime() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (channel_) {
    auto status = StopLocked("shutdown");
    if (!status.ok()) std::cerr << "Shutdown disable unconfirmed: " << status << "\n";
  }
}

absl::Status Runtime::Attach(std::shared_ptr<robot::board::BoardChannel> channel) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (channel_ || !channel)
    return absl::FailedPreconditionError("Already attached or null channel");
  channel_ = std::move(channel);
  auto status = channel_->Disable();
  disable_acknowledged_ = status.ok();
  if (status.ok()) status = ReadLocked();
  if (!status.ok()) {
    outcome_ = "fault";
    error_ = status.ToString();
    return status;
  }
  // Operator has confirmed that controller counts represent the configured
  // coordinate reference. This is not homing or physical-position verification.
  reference_valid_ = true;
  outcome_ = "ready_disarmed";
  return absl::OkStatus();
}

absl::Status Runtime::ReadLocked() {
  feedback_valid_ = false;
  const auto start = Clock::now();
  auto feedback = channel_->ReadFeedback();
  if (!feedback.ok()) return feedback.status();
  if (Clock::now() - start > std::chrono::milliseconds(device_.exposure.feedback_max_age_ms())) {
    return absl::DeadlineExceededError("Feedback request exceeded freshness budget");
  }
  if (!std::isfinite(feedback->position) || std::abs(feedback->position) > 16777215 ||
      std::round(feedback->position) != feedback->position || feedback->fault_flags != 0) {
    return absl::DataLossError("Invalid controller feedback or controller fault");
  }
  if (reference_valid_) {
    const double next = feedback->position;
    const bool moving = outcome_ == "moving";
    const double lower = moving ? std::min(steps_, target_) : steps_;
    const double upper = moving ? std::max(steps_, target_) : steps_;
    if (next < lower || next > upper) {
      return absl::DataLossError(
          "Unexpected counter change; possible reset or competing controller");
    }
  }
  steps_ = feedback->position;
  const double degrees = steps_ / device_.steps_per_degree;
  if (degrees < device_.actuator.operational_lower_limit() ||
      degrees > device_.actuator.operational_upper_limit()) {
    return absl::OutOfRangeError(
        "Controller position " + std::to_string(degrees) + " degrees (" + std::to_string(steps_) +
        " emitted steps) is outside configured operational limits [" +
        std::to_string(device_.actuator.operational_lower_limit()) + ", " +
        std::to_string(device_.actuator.operational_upper_limit()) + "] degrees");
  }
  received_ = Clock::now();
  received_unix_ms_ = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::system_clock::now().time_since_epoch())
                          .count();
  feedback_valid_ = true;
  return absl::OkStatus();
}

absl::Status Runtime::StopLocked(const std::string& outcome) {
  reference_valid_ = false;
  outcome_ = outcome;
  auto status = channel_->Disable();
  disable_acknowledged_ = status.ok();
  if (!status.ok()) {
    error_ = status.ToString();
    outcome_ = "stop_unconfirmed";
  }
  return status;
}

void Runtime::Poll() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (outcome_ != "moving") return;
  if (Clock::now() >= deadline_) {
    error_ = "Move duration exceeded";
    auto status = StopLocked("timed_out");
    (void)status;
    return;
  }
  auto status = ReadLocked();
  if (!status.ok()) {
    error_ = status.ToString();
    auto stopped = StopLocked("fault");
    (void)stopped;
  } else if (Clock::now() >= deadline_) {
    error_ = "Move duration exceeded";
    auto stopped = StopLocked("timed_out");
    (void)stopped;
  } else if (steps_ == target_) {
    outcome_ = "controller_target_reached";
  }
}

Struct Runtime::StateLocked() const {
  Struct out;
  auto& f = *out.mutable_fields();
  f["device_id"] = V(device_.actuator.actuator_name());
  f["connected"] = V(channel_ != nullptr);
  f["reference_valid"] = V(reference_valid_);
  f["physical_position_verified"] = V(false);
  f["feedback_source"] = V("controller_emitted_steps");
  f["status"] = V(outcome_);
  f["command_id"] = V(std::to_string(command_id_));
  f["disable_acknowledged"] = V(disable_acknowledged_);
  const bool fresh =
      feedback_valid_ &&
      Clock::now() - received_ <= std::chrono::milliseconds(device_.exposure.feedback_max_age_ms());
  f["feedback_valid"] = V(fresh);
  if (fresh) {
    f["emitted_steps"] = V(steps_);
    f["estimated_position_degrees"] = V(steps_ / device_.steps_per_degree);
    f["host_received_unix_ms"] = V(static_cast<double>(received_unix_ms_));
  }
  if (command_id_) f["target_steps"] = V(target_);
  if (!error_.empty()) f["last_error"] = V(error_);
  return out;
}

Struct Runtime::DescribeLocked() const {
  Struct out;
  auto& f = *out.mutable_fields();
  f["device_id"] = V(device_.actuator.actuator_name());
  f["description"] = V(device_.exposure.description());
  f["unit"] = V("degrees");
  f["minimum"] = V(static_cast<double>(device_.actuator.operational_lower_limit()));
  f["maximum"] = V(static_cast<double>(device_.actuator.operational_upper_limit()));
  f["max_move_degrees"] = V(device_.exposure.max_move_degrees());
  f["max_move_duration_ms"] = V(static_cast<double>(device_.exposure.max_move_duration_ms()));
  f["feedback_max_age_ms"] = V(static_cast<double>(device_.exposure.feedback_max_age_ms()));
  f["position_resolution_degrees"] = V(1.0 / device_.steps_per_degree);
  f["feedback"] = V(
      "Emitted step count; no encoder, homing, stall detection or physical-position verification.");
  f["write_semantics"] =
      V("Absolute position, rounded to a whole step. Accepted is not completed. One active move; "
        "no queue or automatic retry. Stop requires operator restart/reference confirmation.");
  f["connected"] = V(channel_ != nullptr);
  for (const auto& tag : device_.exposure.tags())
    *f["tags"].mutable_list_value()->add_values() = V(tag);
  for (const char* op : {"read_state", "write_position", "stop_device"})
    *f["operations"].mutable_list_value()->add_values() = V(op);
  return out;
}

Struct Runtime::Handle(const Struct& request) {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto op = Field(request, "operation").string_value();
  if (op == "list_devices") {
    Struct out;
    *(*out.mutable_fields())["devices"].mutable_list_value()->add_values() = V(DescribeLocked());
    return out;
  }
  if (Field(request, "device_id").string_value() != device_.actuator.actuator_name())
    return Error("Unknown device_id");
  if (op == "describe_device") return DescribeLocked();
  if (!channel_)
    return Error(
        "Hardware is offline; operator must start the executor with hardware/reference "
        "confirmation");
  if (op == "stop_device") {
    auto status = StopLocked("stopped");
    (void)status;
    return StateLocked();
  }
  if (op == "read_state") {
    auto status = ReadLocked();
    if (!status.ok()) {
      error_ = status.ToString();
      auto stopped = StopLocked("fault");
      (void)stopped;
    }
    if (outcome_ == "moving") {
      if (Clock::now() >= deadline_) {
        error_ = "Move duration exceeded";
        auto stopped = StopLocked("timed_out");
        (void)stopped;
      } else if (feedback_valid_ && steps_ == target_) {
        outcome_ = "controller_target_reached";
      }
    }
    return StateLocked();
  }
  if (op != "write_position") return Error("Unknown operation");
  if (!reference_valid_)
    return Error(
        "Disarmed: operator must restart and confirm reference; do not retry automatically");
  if (outcome_ == "moving") return Error("A move is already active; read its state or stop it");
  const auto& value = Field(request, "position_degrees");
  if (value.kind_case() != Value::kNumberValue || !std::isfinite(value.number_value()))
    return Error("position_degrees must be a finite number");
  const double degrees = value.number_value();
  const double lo = device_.actuator.operational_lower_limit(),
               hi = device_.actuator.operational_upper_limit();
  if (degrees < lo || degrees > hi) return Error("Position exceeds operational limits");
  const double target = std::round(degrees * device_.steps_per_degree);
  const double quantized = target / device_.steps_per_degree;
  if (quantized < lo || quantized > hi) return Error("Rounded target exceeds operational limits");
  auto status = ReadLocked();
  if (!status.ok()) {
    error_ = status.ToString();
    auto stopped = StopLocked("fault");
    (void)stopped;
    return Error(error_);
  }
  if (std::abs(quantized - steps_ / device_.steps_per_degree) > device_.exposure.max_move_degrees())
    return Error("Move exceeds per-command travel limit");
  // Replace the old target before enabling a previously disabled channel.
  // On success the channel stays enabled to hold its estimated position.
  deadline_ = Clock::now() + std::chrono::milliseconds(device_.exposure.max_move_duration_ms());
  ++command_id_;
  target_ = target;
  status = channel_->SetTarget(robot::board::TargetMode::kPosition, static_cast<float>(target));
  if (status.ok() && Clock::now() >= deadline_) {
    status = absl::DeadlineExceededError("Move duration exceeded before enable");
  }
  if (status.ok()) {
    disable_acknowledged_ = false;
    status = channel_->Enable();
  }
  if (!status.ok()) {
    error_ = status.ToString();
    auto stopped = StopLocked("fault");
    (void)stopped;
    return Error("Command outcome uncertain; restart/reference confirmation required: " + error_);
  }
  error_.clear();
  outcome_ = "moving";
  Struct out = StateLocked();
  (*out.mutable_fields())["accepted"] = V(true);
  return out;
}
}  // namespace mhs
