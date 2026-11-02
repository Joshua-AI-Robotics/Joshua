#pragma once

#include <memory>
#include <vector>

#include "absl/status/statusor.h"
#include "robot/board/interfaces/board_interface.h"
#include "robot/board/proto/board.pb.h"

namespace robot::board {

struct BoardImplementation {
  BoardType type;
  const char* support_scope;
  std::shared_ptr<BoardInterface> (*create)();
};

class BoardFactory {
 public:
  // Fixed descriptors only: no creation, initialization, cache or device access.
  static const std::vector<BoardImplementation>& Implementations();
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
