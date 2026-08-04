#include "robot/board/feetech_bus/feetech_protocol.h"

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"

namespace robot::board::feetech {

namespace {

constexpr uint8_t kInstrPing = 0x01;
constexpr uint8_t kInstrRead = 0x02;
constexpr uint8_t kInstrWrite = 0x03;

uint8_t ComputeChecksum(const std::vector<uint8_t>& frame_from_id) {
  uint32_t sum = 0;
  for (uint8_t byte : frame_from_id) {
    sum += byte;
  }
  return static_cast<uint8_t>(~sum & 0xFF);
}

std::vector<uint8_t> AssemblePacket(uint8_t servo_id,
                                    uint8_t instruction,
                                    const std::vector<uint8_t>& params) {
  const uint8_t length = static_cast<uint8_t>(params.size() + 2);
  std::vector<uint8_t> frame_from_id = {servo_id, length, instruction};
  frame_from_id.insert(frame_from_id.end(), params.begin(), params.end());
  const uint8_t checksum = ComputeChecksum(frame_from_id);

  std::vector<uint8_t> packet = {0xFF, 0xFF};
  packet.insert(packet.end(), frame_from_id.begin(), frame_from_id.end());
  packet.push_back(checksum);
  return packet;
}

}  // namespace

std::vector<uint8_t> BuildPingPacket(uint8_t servo_id) {
  return AssemblePacket(servo_id, kInstrPing, {});
}

std::vector<uint8_t> BuildReadPacket(uint8_t servo_id, uint8_t address, uint8_t length) {
  return AssemblePacket(servo_id, kInstrRead, {address, length});
}

std::vector<uint8_t> BuildWritePacket(uint8_t servo_id,
                                      uint8_t address,
                                      const std::vector<uint8_t>& data) {
  std::vector<uint8_t> params = {address};
  params.insert(params.end(), data.begin(), data.end());
  return AssemblePacket(servo_id, kInstrWrite, params);
}

absl::StatusOr<std::vector<uint8_t>> ParseStatusPacket(const std::vector<uint8_t>& response,
                                                       uint8_t expected_servo_id) {
  if (response.size() < 4) {
    return absl::InternalError("Feetech response too short to contain a header.");
  }
  if (response[0] != 0xFF || response[1] != 0xFF) {
    return absl::InternalError("Feetech response has an invalid header.");
  }
  if (response[2] != expected_servo_id) {
    return absl::InternalError(absl::StrCat("Feetech response is from servo ",
                                            static_cast<int>(response[2]),
                                            ", expected ",
                                            static_cast<int>(expected_servo_id),
                                            "."));
  }
  const uint8_t length = response[3];
  if (length < 2) {
    return absl::InternalError("Feetech response length field is too small.");
  }
  const size_t expected_size = 4 + static_cast<size_t>(length);
  if (response.size() != expected_size) {
    return absl::InternalError(absl::StrCat("Feetech response size ",
                                            response.size(),
                                            " does not match its length field (expected ",
                                            expected_size,
                                            ")."));
  }

  const std::vector<uint8_t> frame_from_id(response.begin() + 2, response.end() - 1);
  const uint8_t checksum_computed = ComputeChecksum(frame_from_id);
  const uint8_t checksum_received = response.back();
  if (checksum_computed != checksum_received) {
    return absl::InternalError("Feetech response checksum mismatch.");
  }

  const uint8_t error = response[4];
  if (error != 0) {
    return absl::InternalError(absl::StrCat(
        "Servo ", static_cast<int>(expected_servo_id), " reported error byte ", error, "."));
  }

  return std::vector<uint8_t>(response.begin() + 5, response.end() - 1);
}

std::vector<uint8_t> EncodeUint16Le(uint16_t value) {
  return {static_cast<uint8_t>(value & 0xFF), static_cast<uint8_t>((value >> 8) & 0xFF)};
}

uint16_t DecodeUint16Le(uint8_t low_byte, uint8_t high_byte) {
  return static_cast<uint16_t>(low_byte) | (static_cast<uint16_t>(high_byte) << 8);
}

}  // namespace robot::board::feetech
