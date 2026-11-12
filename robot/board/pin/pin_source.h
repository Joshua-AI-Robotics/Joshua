#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "absl/status/statusor.h"

namespace robot::board {

enum class PinKind { kGpio, kPower, kGround, kPeripheral, kReserved };

// Documented hardware functions, independent of the configured robot purpose
// and of which Joshua backends are installed. A pin can support several mux
// alternatives; drivers must request one and arrange the corresponding pinmux.
enum class PinFunction { kDigitalInput, kDigitalOutput, kPwm, kI2c, kSpi, kUart, kI2s, kClock };
const char* PinFunctionName(PinFunction function);

struct PwmPinInfo {
  // Stable platform controller identity, not a Linux pwmchip number. Pi uses
  // the device-tree node label; Jetson uses NVIDIA's controller device name.
  std::string controller;
  uint32_t channel;
};

// A physical connector position, including supply and ground pins. This is the
// reusable hardware source for drivers; it requires no GPIO config or device I/O.
struct PinInfo {
  uint32_t header_pin;
  std::string signal;
  PinKind kind;
  uint32_t voltage_mv;  // Nominal domain; 0 means unknown/not applicable.
  std::vector<PinFunction> functions;
  std::string chip_label;
  std::string line_name;
  uint32_t documented_offset = 0;  // Informational; runtime resolves line_name.
  std::optional<PwmPinInfo> pwm;
  std::string alternatives;
  std::string restrictions;
  bool Supports(PinFunction function) const;
};

struct PinProfile {
  std::string id;
  uint32_t version;
  std::string model;
  std::string connector;
  std::vector<std::string> compatible;  // Exact device-tree compatible tokens.
  std::vector<PinInfo> pins;            // Every physical connector position.
  std::vector<std::string> sources;

  // Supply/ground/reserved positions remain inspectable. RequirePin additionally
  // checks the requested function and rejects reserved positions for drivers.
  absl::StatusOr<const PinInfo*> FindPin(uint32_t header_pin) const;
  absl::StatusOr<const PinInfo*> RequirePin(uint32_t header_pin, PinFunction function) const;
};

const std::vector<PinProfile>& PinProfiles();
absl::StatusOr<const PinProfile*> FindPinProfile(const std::string& id, uint32_t version);

}  // namespace robot::board
