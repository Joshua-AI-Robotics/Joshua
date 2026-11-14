#pragma once

#include <map>
#include <memory>
#include <mutex>
#include <string>

#include "robot/board/host_gpio/gpio_backend.h"
#include "robot/board/interfaces/board_interface.h"

namespace robot::board {

class HostGpioBoard final : public BoardInterface {
 public:
  HostGpioBoard();
  explicit HostGpioBoard(std::shared_ptr<GpioBackend> backend);
  ~HostGpioBoard() override;
  absl::Status Init(const Board& config) override;
  absl::StatusOr<std::shared_ptr<BoardChannel>> OpenChannel(uint32_t index) override;
  absl::StatusOr<std::shared_ptr<DigitalPin>> OpenDigitalPin(const std::string& name) override;
  absl::Status Teardown() override;
  absl::Status EmergencyStop() override;

 private:
  class Pin;
  absl::Status ClosePins();  // Caller holds mutex_; reverse acquisition order.
  std::shared_ptr<GpioBackend> backend_;
  std::mutex mutex_;
  bool initialized_ = false;
  bool used_ = false;  // One lifecycle; deliberate reconstruction after teardown/fault.
  std::map<std::string, std::shared_ptr<Pin>> pins_;
  std::vector<std::shared_ptr<Pin>> acquisition_order_;
};

}  // namespace robot::board
