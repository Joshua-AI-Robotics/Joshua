#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "absl/status/statusor.h"
#include "robot/board/proto/board.pb.h"

namespace robot::board {

enum class PinKind { kGpio, kPower, kGround, kReserved };

// Immutable hardware facts. An alternative function describes the connector;
// it does not advertise a driver, current pinmux, or qualified electrical load.
struct PinInfo {
  uint32_t header_pin;
  std::string signal;
  PinKind kind;
  uint32_t voltage_mv;  // Nominal domain; 0 means unknown/not applicable.
  bool digital_input = false;
  bool digital_output = false;
  std::string chip_label;
  std::string line_name;
  uint32_t documented_offset = 0;  // Informational; runtime resolves line_name.
  std::string alternatives;
  std::string restrictions;
};

struct PinProfile {
  std::string id;
  uint32_t version;
  std::string model;
  std::string connector;
  std::vector<std::string> compatible;  // Exact device-tree compatible tokens.
  std::vector<PinInfo> pins;            // Every claimed connector position, including power.
  std::vector<std::string> sources;
};

// A configured claim: physical identity and intended use travel together.
struct PinResource {
  std::string profile_id;
  uint32_t profile_version;
  std::string connector;
  PinInfo info;
  DigitalPinConfig config;
  std::string identity() const;
};

const std::vector<PinProfile>& PinProfiles();
absl::StatusOr<const PinProfile*> FindPinProfile(const std::string& id, uint32_t version);
absl::StatusOr<PinResource> ResolvePin(const PinProfile& profile, const DigitalPinConfig& config);
// Pure validation/resolution: does not inspect /dev or acquire any resource.
absl::StatusOr<std::vector<PinResource>> ResolveHostGpioPins(const Board& config);

}  // namespace robot::board
