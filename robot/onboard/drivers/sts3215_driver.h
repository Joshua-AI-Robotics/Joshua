#pragma once

#include <boost/asio.hpp>
#include <vector>
#include "robot/comm_interface/serial/serial.h"
#include "robot/onboard/interfaces/motor_interface.h"

class Sts3215Driver : public MotorInterface {
  public:
  Sts3215Driver(std::unique_ptr<Serial> serial);
  ~Sts3215Driver();
  void SetSpeed(int servo_id, float value) override;
  void SetPosition(int servo_id, float angle) override;
  void SetTorque(int servo_id, float torque) override;

  
  uint8_t calculate_checksum(const std::vector<uint8_t>& data);
  std::vector<uint8_t> create_move_packet(uint8_t servo_id, uint16_t position, uint16_t speed);
  std::vector<uint8_t> create_torque_packet(uint8_t servo_id, uint8_t enable);
  std::vector<uint8_t> create_read_position_packet(uint8_t servo_id);
  uint16_t read_servo_position(uint8_t servo_id);
private: 
  std::unique_ptr<Serial> serial_;
  float move_speed_;
  float move_time_in_ms_;
};