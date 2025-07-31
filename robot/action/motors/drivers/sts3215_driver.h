#pragma once

#include <boost/asio.hpp>
#include <vector>
#include "robot/comm_interface/serial/serial.h"
#include "robot/action/interfaces/actuator_interface.h"
#include "config/proto/robot.pb.h"
#include <memory>

namespace robot::action{
class Sts3215Driver : public robot::action::ActuatorInterface {
  public:
  Sts3215Driver(const std::shared_ptr<robot::comm_interface::Serial>& serial, const robot::action::Actuator& action_config);
  ~Sts3215Driver();
  
  // ActionInterface methods
  void SetAction(const robot::action::ActionPacket& action_packet) override;
  std::string GetId() override; // TODO: Update the ID scheme.
  
  // ActuatorInterface methods
  void SetSpeed(float value) override;
  void SetPosition(float angle) override;
  void SetTorque(float torque) override;
  void SetMiddlePosition() override;
  void SetIdlePosition() override;
  void GracefulShutdown() override;

  private: 
  uint8_t calculate_checksum(std::vector<uint8_t>::const_iterator begin, std::vector<uint8_t>::const_iterator end);
  std::vector<uint8_t> create_move_packet(uint16_t position);
  std::vector<uint8_t> create_torque_packet(uint8_t enable);

  std::shared_ptr<robot::comm_interface::Serial> serial_;
  std::string id_;
  uint8_t servo_id_;
  uint16_t move_speed_;
  uint16_t move_time_in_ms_;
  uint16_t physical_lower_limit_;
  uint16_t physical_upper_limit_;
  uint16_t operational_lower_limit_;
  uint16_t operational_upper_limit_;
  uint16_t idle_position_;
  uint16_t current_position_;
};
}