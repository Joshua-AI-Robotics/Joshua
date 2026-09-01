#pragma once

#include <functional>
#include <map>
#include <memory>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "robot/board/frame/frame_transport.h"
#include "robot/board/interfaces/board_interface.h"
#include "robot/board/joshua_wire/joshua_wire_board.h"
#include "robot/board/proto/board.pb.h"
#include "robot/comm/proto/comm.pb.h"

namespace robot::board {

struct Am243SharedState;

// AM243 supports two explicit firmware/transport variants:
// - SERIAL: Joshua-authored joshua_wire_v1 firmware, delegated unchanged to
//   the shared JoshuaWireBoard implementation.
// - ETHERCAT: the retained TI demo PDO mapping and SOEM lifecycle.
// The config's comm_type selects the variant; the filename does not.
class Am243Board : public BoardInterface {
 public:
  Am243Board() = default;

  absl::Status Init(const robot::board::Board& config) override;
  absl::StatusOr<std::shared_ptr<BoardChannel>> OpenChannel(uint32_t index) override;
  absl::Status Teardown() override;

  static void SetFrameTransportFactoryForTesting(
      std::function<absl::StatusOr<std::shared_ptr<FrameTransport>>(const robot::comm::Comm&)>
          factory) {
    JoshuaWireBoard::SetFrameTransportFactoryForTesting(std::move(factory));
  }

 private:
  bool initialized_ = false;
  bool serial_mode_ = false;
  robot::board::Board config_;
  std::shared_ptr<JoshuaWireBoard> serial_board_;
  std::shared_ptr<Am243SharedState> state_;
  std::map<uint32_t, std::shared_ptr<BoardChannel>> channels_;
};

}  // namespace robot::board
