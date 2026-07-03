#pragma once

#include <memory>
#include <string>

#include "config/proto/robot.pb.h"
#include "robot/action/interfaces/actuator_interface.h"
#include "robot/comm/ethercat/ethercat_transport.h"

namespace robot::action {

// Actuator driver skeleton for AM243-backed motors reached over EtherCAT PDOs.
//
// This class owns only the AM243 actuator mapping. Generic EtherCAT discovery,
// state transitions, cyclic process-data exchange, and the split LRD/LWR policy
// live under robot/comm/ethercat/.
class Am243EthercatDriver : public robot::action::ActuatorInterface {
 public:
  Am243EthercatDriver(const std::shared_ptr<robot::comm::ethercat::EthercatTransport>& ethercat,
                      const robot::action::Actuator& action_config);
  ~Am243EthercatDriver() override = default;

  // ActionInterface methods.
  absl::Status Init() override;
  std::string GetId() override;
  absl::Status SetAction(const robot::action::ActionPacket& action_packet) override;
  absl::Status Teardown() override;

  // ActuatorInterface methods.
  absl::Status SetSpeed(float value) override;
  absl::Status SetPosition(float angle) override;
  absl::Status SetTorque(float torque) override;
  absl::Status SetMiddlePosition() override;
  absl::Status SetIdlePosition() override;

 private:
  std::shared_ptr<robot::comm::ethercat::EthercatTransport> ethercat_;
  robot::action::Actuator action_config_;
  robot::comm::ethercat::PdoRegion pdo_region_;
  std::string id_;
  float speed_ = 0.0f;
  float torque_ = 0.0f;
  float physical_lower_limit_ = 0.0f;
  float physical_upper_limit_ = 0.0f;
  float operational_lower_limit_ = 0.0f;
  float operational_upper_limit_ = 0.0f;
  float idle_position_ = 0.0f;
};

}  // namespace robot::action
