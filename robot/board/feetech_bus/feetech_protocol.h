#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "absl/status/statusor.h"

namespace robot::board::feetech {

// Feetech STS/SCS register map used by FeetechBusBoard. Addresses match the
// public STS3215 memory map; unverified against real hardware on this
// branch (no LP servo bus wired up here — same caveat as the AM243 port's
// pending hardware smoke run, docs/BOARD_LAYER_RFC.md §10 Phase 3).
inline constexpr uint8_t kRegTorqueEnable = 0x28;
inline constexpr uint8_t kRegGoalPosition = 0x2A;
// Bundled into the same WRITE_DATA burst as kRegGoalPosition (contiguous
// registers), matching the wire shape the pre-board-layer Sts3215Driver used.
inline constexpr uint8_t kRegMovingTime = 0x2C;
inline constexpr uint8_t kRegMovingSpeed = 0x2E;
inline constexpr uint8_t kRegPresentPosition = 0x38;
inline constexpr uint8_t kRegModelNumber = 0x03;

// Bytes on the wire for a status/response packet with zero parameter bytes:
// 0xFF 0xFF id length error checksum.
inline constexpr size_t kStatusPacketOverheadBytes = 6;

// Builds a PING instruction packet (docs/BOARD_LAYER_RFC.md §5.6 IDENTIFY).
std::vector<uint8_t> BuildPingPacket(uint8_t servo_id);

// Builds a READ_DATA instruction packet requesting `length` bytes starting
// at `address`.
std::vector<uint8_t> BuildReadPacket(uint8_t servo_id, uint8_t address, uint8_t length);

// Builds a WRITE_DATA instruction packet writing `data` starting at
// `address`. Multi-byte registers are little-endian, matching the STS3215
// wire format (e.g. goal position + moving time + moving speed bundled into
// one 6-byte burst starting at kRegGoalPosition).
std::vector<uint8_t> BuildWritePacket(uint8_t servo_id, uint8_t address,
                                     const std::vector<uint8_t>& data);

// Validates header, servo id, checksum, and error byte on a status/response
// packet, returning the parameter bytes (everything between the error byte
// and the checksum). Used for PING acks and READ_DATA responses.
absl::StatusOr<std::vector<uint8_t>> ParseStatusPacket(const std::vector<uint8_t>& response,
                                                       uint8_t expected_servo_id);

std::vector<uint8_t> EncodeUint16Le(uint16_t value);
uint16_t DecodeUint16Le(uint8_t low_byte, uint8_t high_byte);

}  // namespace robot::board::feetech
