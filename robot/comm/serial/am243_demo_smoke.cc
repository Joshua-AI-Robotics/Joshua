// Transport-level serial smoke test for an AM243 running joshua_wire_v1.
// This sends CONFIGURE/ENABLE/SET_TARGET commands. Confirm the connected
// firmware and hardware are safe before running it.
//
// Usage:
//   bazel run //robot/comm/serial:am243_demo_smoke --
//     <port> [cycles] [channel] [period_ms]

#include <boost/asio/io_context.hpp>

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "firmware/common/joshua_wire_v1.h"
#include "robot/comm/serial/serial.h"

namespace {

constexpr int kBaudrate = 115200;

void PrintUsage(const char* argv0) {
  std::cerr << "usage: " << argv0 << " <port> [cycles] [channel] [period_ms]\n"
            << "example: " << argv0 << " /dev/ttyACM0 10 0 250\n";
}

int ParsePositiveInt(const char* value, int fallback) {
  char* end = nullptr;
  const long parsed = std::strtol(value, &end, 10);
  if (end == value || *end != '\0' || parsed <= 0) {
    return fallback;
  }
  return static_cast<int>(parsed);
}

int ParseChannel(const char* value, int fallback) {
  char* end = nullptr;
  const long parsed = std::strtol(value, &end, 10);
  if (end == value || *end != '\0' || parsed < 0 || parsed >= JW1_MAX_CHANNELS) {
    return fallback;
  }
  return static_cast<int>(parsed);
}

int Fail(const absl::Status& status) {
  std::cerr << "error: " << status << "\n";
  return 1;
}

std::string FirmwareName(const jw1_identify_response_t& identify) {
  const size_t length = strnlen(identify.fw_name, JW1_FW_NAME_LEN);
  return std::string(identify.fw_name, length);
}

const char* DriveName(jw1_drive_t drive) {
  switch (drive) {
    case JW1_DRIVE_STEP_DIR:
      return "STEP_DIR";
    case JW1_DRIVE_PWM_DC:
      return "PWM_DC";
    case JW1_DRIVE_SERVO_BUS_UART:
      return "SERVO_BUS_UART";
    case JW1_DRIVE_CAN:
      return "CAN";
    case JW1_DRIVE_PDO_JOINT:
      return "PDO_JOINT";
    default:
      return "INVALID";
  }
}

absl::Status WireStatus(jw1_status_t status, const std::string& operation) {
  switch (status) {
    case JW1_STATUS_OK:
      return absl::OkStatus();
    case JW1_STATUS_UNSUPPORTED:
      return absl::UnimplementedError(operation + ": firmware reports unsupported");
    case JW1_STATUS_ERROR:
    default:
      return absl::InternalError(operation + ": firmware reports error");
  }
}

absl::StatusOr<std::vector<uint8_t>> Exchange(robot::comm::Serial* serial,
                                              const uint8_t* request,
                                              int request_length,
                                              size_t response_length) {
  if (request_length < 0) {
    return absl::InternalError("failed to encode joshua_wire_v1 request");
  }
  return serial->AtomicRead(std::vector<uint8_t>(request, request + request_length),
                            response_length);
}

absl::Status ExchangeStatus(robot::comm::Serial* serial,
                            const uint8_t* request,
                            int request_length,
                            const std::string& operation) {
  auto response_or = Exchange(
      serial, request, request_length, JW1_FRAME_LEN(JW1_STATUS_RESPONSE_PAYLOAD_LEN));
  if (!response_or.ok()) {
    return response_or.status();
  }

  jw1_frame_t frame;
  if (jw1_decode_frame(response_or->data(), response_or->size(), &frame) != 0) {
    return absl::InternalError(operation + ": malformed response frame");
  }
  jw1_status_t status;
  if (jw1_decode_status_response(&frame, &status) != 0) {
    return absl::InternalError(operation + ": malformed status response");
  }
  return WireStatus(status, operation);
}

absl::Status Configure(robot::comm::Serial* serial, uint8_t channel) {
  const jw1_configure_step_dir_t config = {
      .max_pulse_rate_hz = 4000,
      .invert_dir = 0,
      .enable_active_low = 1,
      .step_pin = 2,
      .dir_pin = 3,
      .enable_pin = 4,
      .step_pulse_width_us = 0,
  };
  uint8_t request[JW1_MAX_FRAME_LEN];
  const int request_length = jw1_encode_configure_channel_step_dir(
      request, sizeof(request), channel, &config);
  return ExchangeStatus(serial, request, request_length, "CONFIGURE_CHANNEL");
}

absl::Status SetTarget(robot::comm::Serial* serial, uint8_t channel, float target) {
  uint8_t request[JW1_MAX_FRAME_LEN];
  const int request_length = jw1_encode_set_target(
      request, sizeof(request), channel, JW1_MODE_POSITION, target);
  return ExchangeStatus(serial, request, request_length, "SET_TARGET");
}

absl::StatusOr<jw1_feedback_t> ReadFeedback(robot::comm::Serial* serial, uint8_t channel) {
  uint8_t request[JW1_MAX_FRAME_LEN];
  const int request_length =
      jw1_encode_get_feedback_request(request, sizeof(request), channel);
  auto response_or = Exchange(
      serial, request, request_length, JW1_FRAME_LEN(JW1_FEEDBACK_RESPONSE_PAYLOAD_LEN));
  if (!response_or.ok()) {
    return response_or.status();
  }

  jw1_frame_t frame;
  if (jw1_decode_frame(response_or->data(), response_or->size(), &frame) != 0) {
    return absl::InternalError("GET_FEEDBACK: malformed response frame");
  }
  jw1_feedback_t feedback;
  if (jw1_decode_feedback_response(&frame, &feedback) != 0) {
    return absl::InternalError("GET_FEEDBACK: malformed feedback response");
  }
  return feedback;
}

absl::Status SetEnabled(robot::comm::Serial* serial, uint8_t channel, bool enabled) {
  uint8_t request[JW1_MAX_FRAME_LEN];
  const int request_length = enabled ? jw1_encode_enable(request, sizeof(request), channel)
                                     : jw1_encode_disable(request, sizeof(request), channel);
  return ExchangeStatus(serial, request, request_length, enabled ? "ENABLE" : "DISABLE");
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2 || argc > 5) {
    PrintUsage(argv[0]);
    return 2;
  }

  const std::string port = argv[1];
  const int cycles = argc >= 3 ? ParsePositiveInt(argv[2], 10) : 10;
  const uint8_t channel =
      static_cast<uint8_t>(argc >= 4 ? ParseChannel(argv[3], 0) : 0);
  const int period_ms = argc >= 5 ? ParsePositiveInt(argv[4], 250) : 250;

  auto io_context = std::make_shared<boost::asio::io_context>();
  robot::comm::Serial serial(io_context, port, kBaudrate);
  absl::Status status = serial.Open();
  if (!status.ok()) {
    return Fail(status);
  }

  uint8_t identify_request[JW1_MAX_FRAME_LEN];
  const int identify_request_length =
      jw1_encode_identify_request(identify_request, sizeof(identify_request));
  auto identify_response_or = Exchange(&serial,
                                       identify_request,
                                       identify_request_length,
                                       JW1_FRAME_LEN(JW1_IDENTIFY_RESPONSE_PAYLOAD_LEN));
  if (!identify_response_or.ok()) {
    return Fail(identify_response_or.status());
  }

  jw1_frame_t identify_frame;
  if (jw1_decode_frame(
          identify_response_or->data(), identify_response_or->size(), &identify_frame) != 0) {
    return Fail(absl::InternalError("IDENTIFY: malformed response frame"));
  }
  jw1_identify_response_t identify;
  if (jw1_decode_identify_response(&identify_frame, &identify) != 0) {
    return Fail(absl::InternalError("IDENTIFY: malformed response payload"));
  }

  std::cout << "serial port=" << port << " baud=" << kBaudrate
            << " protocol=joshua_wire_v1/" << static_cast<int>(identify_frame.proto_ver) << "\n";
  std::cout << "board id=" << static_cast<int>(identify.board_id) << " firmware=\""
            << FirmwareName(identify) << "\" channels=" << static_cast<int>(identify.n_channels)
            << "\n";
  for (uint8_t index = 0; index < identify.n_channels; ++index) {
    std::cout << "channel " << static_cast<int>(index)
              << " drive=" << DriveName(identify.channel_drives[index]) << "\n";
  }

  if (identify.board_id != JW1_BOARD_AM243) {
    return Fail(absl::FailedPreconditionError("IDENTIFY did not report an AM243 board"));
  }
  if (channel >= identify.n_channels || identify.channel_drives[channel] != JW1_DRIVE_STEP_DIR) {
    return Fail(absl::FailedPreconditionError(
        "selected channel is not an available STEP_DIR channel"));
  }

  status = Configure(&serial, channel);
  std::cout << "configure channel=" << static_cast<int>(channel) << " step=2 dir=3 enable=4: "
            << status << "\n";
  if (!status.ok()) {
    return 1;
  }
  status = SetEnabled(&serial, channel, true);
  std::cout << "enable channel=" << static_cast<int>(channel) << ": " << status << "\n";
  if (!status.ok()) {
    return 1;
  }

  for (int cycle = 0; cycle < cycles; ++cycle) {
    const float target = cycle % 2 == 0 ? 500.0f : -500.0f;
    const auto started = std::chrono::steady_clock::now();
    status = SetTarget(&serial, channel, target);
    if (!status.ok()) {
      (void)SetEnabled(&serial, channel, false);
      return Fail(status);
    }
    auto feedback_or = ReadFeedback(&serial, channel);
    if (!feedback_or.ok()) {
      (void)SetEnabled(&serial, channel, false);
      return Fail(feedback_or.status());
    }
    const auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(
                                std::chrono::steady_clock::now() - started)
                                .count();
    std::cout << std::fixed << std::setprecision(1) << "cycle=" << cycle
              << " target=" << target << " position=" << feedback_or->position
              << " velocity=" << feedback_or->velocity << " faults=0x" << std::hex
              << std::setw(4) << std::setfill('0') << feedback_or->fault_flags << std::dec
              << std::setfill(' ') << " roundtrip_us=" << elapsed_us << "\n";
    if (cycle + 1 < cycles) {
      std::this_thread::sleep_for(std::chrono::milliseconds(period_ms));
    }
  }

  status = SetEnabled(&serial, channel, false);
  std::cout << "disable channel=" << static_cast<int>(channel) << ": " << status << "\n";
  return status.ok() ? 0 : 1;
}
