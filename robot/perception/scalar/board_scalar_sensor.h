#pragma once
#include <memory>
#include <string>

#include "robot/board/interfaces/scalar_sensor.h"
#include "robot/perception/interfaces/perception_interface.h"
#include "utils/status_macros.h"

namespace robot::perception {
class BoardScalarSensor : public PerceptionInterface {
 public:
  BoardScalarSensor(std::shared_ptr<robot::board::ScalarSensor> source, std::string name)
      : source_(std::move(source)), name_(std::move(name)) {}
  ~BoardScalarSensor() override {
    Teardown().IgnoreError();
  }
  absl::Status Init() override {
    return absl::OkStatus();
  }
  std::string GetId() override {
    return name_;
  }
  absl::Status Teardown() override {
    return source_->Close();
  }
  absl::StatusOr<PerceptionPacket> GetData() override {
    ABSL_ASSIGN_OR_RETURN(auto o, source_->Latest());
    PerceptionPacket packet;
    packet.set_perception_id(name_);
    packet.set_sequence_id(o.sequence);
    // timestamp_ns remains unset: there is no device-to-ROS clock mapping.
    auto* s = packet.mutable_scalar();
    s->set_value(o.value);
    s->set_unit(o.unit);
    s->set_validity(o.validity);
    s->set_clock(o.clock);
    s->set_boot_id(o.boot_id);
    s->set_acquisition_us(o.acquisition_us);
    s->set_acquisition_sequence(o.sequence);
    s->set_function_id(o.function_id);
    s->set_session_id(o.session_id);
    s->set_subscription_id(o.subscription_id);
    s->set_period_us(o.period_us);
    s->set_dropped_acquisitions(o.dropped_acquisitions);
    s->set_sequence_gaps(o.sequence_gaps);
    s->set_overwritten(o.overwritten);
    s->set_host_receipt_steady_ns(
        std::chrono::duration_cast<std::chrono::nanoseconds>(o.received_at.time_since_epoch())
            .count());
    s->set_receipt_age_us(o.receipt_age.count());
    s->set_receipt_fresh(o.receipt_fresh);
    s->set_usable(o.usable);
    return packet;
  }

 private:
  std::shared_ptr<robot::board::ScalarSensor> source_;
  std::string name_;
};
}  // namespace robot::perception
