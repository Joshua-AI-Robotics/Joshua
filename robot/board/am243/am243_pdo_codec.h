#pragma once

#include <cstdint>
#include <vector>

#include "absl/status/statusor.h"

namespace robot::board::am243 {

constexpr int kDemoPdoSizeBytes = 8;
constexpr int kDemoSeedByteOffset = 0;

// Encodes the currently validated TI EtherCAT simple-demo output PDO shape.
//
// The demo exposes 64 bits of output process data. Joshua uses byte 0 as the
// command seed and leaves bytes 1-7 zero because their TI demo contents do not
// represent Joshua actuator fields.
std::vector<uint8_t> EncodeDemoOutputSeed(uint8_t seed);

// Decodes the currently validated TI EtherCAT simple-demo input PDO echo byte.
absl::StatusOr<uint8_t> DecodeDemoInputEchoSeed(const std::vector<uint8_t>& input_pdo);

}  // namespace robot::board::am243
