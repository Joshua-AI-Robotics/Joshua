#pragma once

#include <boost/asio.hpp>
#include <vector>
#include "robot/comm_interface/serial/serial.h"
#include "robot/onboard/interfaces/motor_interface.h"

class Sts3215Driver : public MotorInterface {
  public:
  Sts3215Driver(const std::shared_ptr<Serial>& serial, int servo_id);
  ~Sts3215Driver();
  void SetSpeed(float value) override;
  void SetPosition(float angle) override;
  void SetTorque(float torque) override;

  
  uint8_t calculate_checksum(const std::vector<uint8_t>& data);
  std::vector<uint8_t> create_move_packet(uint16_t position, uint16_t speed);
  std::vector<uint8_t> create_torque_packet(uint8_t enable);
  std::vector<uint8_t> create_read_position_packet();
  uint16_t read_servo_position();
private: 
  std::shared_ptr<Serial> serial_;
  uint8_t servo_id_;
  float move_speed_;
  float move_time_in_ms_;
};