#pragma once

#include <memory>

#include "absl/status/statusor.h"
#include "robot/board/interfaces/board_interface.h"
#include "robot/board/proto/board.pb.h"

namespace robot::board {

class BoardFactory {
 public:
  // Returns a cached instance per board name — two actuators on the same
  // board share one instance and one comm handle (docs/BOARD_LAYER_RFC.md
  // §5.3). The first call for a name creates the board and runs Init();
  // later calls return the same instance, and fail if their config names a
  // different board_type than the cached one.
  static absl::StatusOr<std::shared_ptr<BoardInterface>> GetOrCreate(
      const robot::board::Board& config);

  // Tears down and forgets every cached board. For tests.
  static void ResetForTesting();

  ~BoardFactory() = default;
  BoardFactory(const BoardFactory&) = delete;
  BoardFactory& operator=(const BoardFactory&) = delete;

 private:
  BoardFactory() = default;
};

}  // namespace robot::board
