#pragma once

#include <cstddef>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace robot::comm {

// A continuous byte stream to one device that multiplexes nothing: a
// scanning lidar that pushes frames, a GPS that emits sentences. The device
// owns the link, so there is no channel to address and no bus mutex to
// share — which is exactly what makes such a device not a board.
//
// This is the third transport shape in the stack, alongside the two the
// board layer needs (docs/BOARD_LAYER_RFC.md): a MessageTransport answers
// one request with one response, a CyclicTransport swaps a fixed image
// every cycle, and a StreamTransport just carries bytes that arrive when
// the device decides.
//
// Sensor drivers depend on this, never on Serial, so the comm axis stays a
// config choice: the same LDS-01 parser works over serial today and over
// UDP the day CommFactory can build one.
class StreamTransport {
 public:
  virtual ~StreamTransport() = default;
  virtual absl::Status Open() = 0;
  virtual absl::Status Write(const std::vector<uint8_t>& data) = 0;
  virtual absl::StatusOr<std::vector<uint8_t>> Read(size_t bytes_to_read) = 0;
};

}  // namespace robot::comm
