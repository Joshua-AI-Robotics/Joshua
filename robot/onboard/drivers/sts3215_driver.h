#pragma once

#include <boost/asio.hpp>
#include <vector>
#include "robot/comm_interface/serial/serial.h"
#include "robot/onboard/interfaces/motor_interface.h"
#include "robot/config/robot.pb.h"

namespace robot::onboard{
class Sts3215Driver : public robot::onboard::MotorInterface {
  public:
  Sts3215Driver(const std::shared_ptr<robot::comm_interface::Serial>& serial, robot_config::Motor motor_config);
  ~Sts3215Driver();
  void SetSpeed(float value) override;
  void SetPosition(float angle) override;
  void SetTorque(float torque) override;

  void SetMiddlePosition();
  void SetIdlePosition();

  private: 
  uint8_t calculate_checksum(std::vector<uint8_t>::const_iterator begin, std::vector<uint8_t>::const_iterator end);
  std::vector<uint8_t> create_move_packet(uint16_t position);
  std::vector<uint8_t> create_torque_packet(uint8_t enable);
  std::vector<uint8_t> create_read_position_packet();
  uint16_t read_servo_position();

  std::shared_ptr<robot::comm_interface::Serial> serial_;
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