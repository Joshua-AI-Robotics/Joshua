#pragma once

#include <cstdint>
#include <vector>

#include "absl/status/statusor.h"

namespace robot::action::am243 {

constexpr int kDemoPdoSizeBytes = 8;
constexpr int kDemoSeedByteOffset = 0;

// Encodes the currently validated TI EtherCAT simple-demo output PDO shape.
//
// The demo exposes 64 bits of output process data. During bring-up, all output
// bytes were written as a walking pattern based on the command seed, and input
// byte 0 echoed the seed later. This helper captures only that known demo
// behavior; it is not an actuator command layout.
std::vector<uint8_t> EncodeDemoOutputWalk(uint8_t seed);

// Decodes the currently validated TI EtherCAT simple-demo input PDO echo byte.
absl::StatusOr<uint8_t> DecodeDemoInputEchoSeed(const std::vector<uint8_t>& input_pdo);

}  // namespace robot::action::am243
