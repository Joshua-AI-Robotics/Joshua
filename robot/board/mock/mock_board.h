#pragma once

#include <map>
#include <memory>
#include <mutex>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "robot/board/interfaces/board_interface.h"
#include "robot/board/proto/board.pb.h"

namespace robot::board {

// BoardType::MOCK — channels are in-memory fakes. No comm, no firmware;
// SetTarget stores the value and ReadFeedback echoes it, so motor drivers
// and the factory seam are testable without hardware.
class MockBoard : public BoardInterface {
 public:
  MockBoard() = default;

  absl::Status Init(const robot::board::Board& config) override;
  absl::StatusOr<std::shared_ptr<BoardChannel>> OpenChannel(uint32_t index) override;
  absl::Status Teardown() override;

 private:
  std::mutex mutex_;
  bool initialized_ = false;
  robot::board::Board config_;
  std::map<uint32_t, std::shared_ptr<BoardChannel>> channels_;
};

}  // namespace robot::board
