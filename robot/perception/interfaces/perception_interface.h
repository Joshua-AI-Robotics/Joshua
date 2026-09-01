#pragma once

#include <memory>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "robot/perception/proto/perception_packet.pb.h"

namespace robot::perception {

// The perception layer's single seam. Every sensor is this and nothing
// more: something that can be started, identified, sampled and stopped.
//
// The two ways a sensor reaches hardware live entirely behind it, and
// neither leaks into a publisher (docs/BOARD_LAYER_RFC.md §5.3):
//   - board-attached — the sensor holds a robot::board::BoardChannel, and
//     the board owns the port, the wire protocol and the bus mutex;
//   - single-stream — the sensor holds a robot::comm::StreamTransport,
//     because a device that multiplexes nothing is not a board.
//
// A concrete sensor is named for what it measures, never for how it is
// wired, so a new link or a new signal leg adds no class here.
class PerceptionInterface {
 public:
  PerceptionInterface() = default;
  virtual ~PerceptionInterface() = default;

  virtual absl::Status Init() = 0;
  virtual std::string GetId() = 0;
  virtual absl::StatusOr<robot::perception::PerceptionPacket> GetData() = 0;
  virtual absl::Status Teardown() = 0;
};

}  // namespace robot::perception
