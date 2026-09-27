// Manual serial-v2 validation entry point. No hardware is opened for help,
// dry-run or invalid options. Real I/O requires explicit hardware confirmation.
#include <gflags/gflags.h>

#include <chrono>
#include <csignal>
#include <iostream>
#include <thread>

#include "absl/strings/numbers.h"
#include "config/config_utils.h"
#include "robot/board/joshua_wire/serial_v2_validation.h"
#include "robot/comm/factory/comm_factory.h"
#include "utils/status_macros.h"

DEFINE_string(config, "", "Joshua Config pbtxt; only the selected board is opened.");
DEFINE_string(board, "", "Exact board name in config.robot.boards.");
DEFINE_string(mode, "handshake", "handshake, configure (disabled), or exercise (may move motors).");
DEFINE_int32(channel, -1, "Configured channel; required for configure/exercise.");
DEFINE_string(target_steps,
              "",
              "Absolute native-step position; required for exercise. No joint-limit enforcement.");
DEFINE_bool(allow_enable, false, "Explicitly permit enabling outputs in exercise mode.");
DEFINE_bool(
    confirm_hardware,
    false,
    "Confirm setup is ready and no other process owns this port. Reset/ESTOP affect all channels.");
DEFINE_bool(dry_run, false, "Validate and print the selected operation without opening hardware.");
DEFINE_int32(sessions, 1, "Fresh reset/identify sessions, 1..10; does not reopen the serial port.");
DEFINE_int32(settle_ms, 2000, "Diagnostic post-open boot wait, 0..10000 ms.");

namespace {
volatile std::sig_atomic_t interrupted = 0;
void Interrupt(int) {
  interrupted = 1;
}

absl::Status Run() {
  using namespace robot::board::diagnostics;
  if (FLAGS_config.empty() || FLAGS_board.empty()) {
    return absl::InvalidArgumentError("--config and --board are required.");
  }
  ABSL_ASSIGN_OR_RETURN(auto config, config::config_util::LoadConfig(FLAGS_config));
  const robot::board::Board* board = nullptr;
  for (const auto& candidate : config.robot().boards()) {
    if (candidate.name() != FLAGS_board) continue;
    if (board != nullptr) return absl::InvalidArgumentError("Board name is not unique.");
    board = &candidate;
  }
  if (board == nullptr) return absl::NotFoundError("Board is not in config.robot.boards.");
  SerialV2ValidationOptions options;
  options.mode = FLAGS_mode;
  options.channel = FLAGS_channel;
  options.allow_enable = FLAGS_allow_enable;
  options.sessions = FLAGS_sessions;
  options.settle_ms = FLAGS_settle_ms;
  if (!FLAGS_target_steps.empty()) {
    float target;
    if (!absl::SimpleAtof(FLAGS_target_steps, &target))
      return absl::InvalidArgumentError("Invalid target_steps.");
    options.target_steps = target;
  }
  ABSL_RETURN_IF_ERROR(ValidateSerialV2Options(*board, options));
  std::cout << "board=" << board->name() << " port=" << board->comm().serial_config().port()
            << " baud=" << board->comm().serial_config().baudrate()
            << " protocol=v2 mode=" << options.mode << " sessions=" << options.sessions
            << " settle_ms=" << options.settle_ms << '\n';
  std::cout << "Reset clears ALL channel configuration; cleanup sends board-wide ESTOP.\n"
            << "No watchdog: lost communication/process termination can leave outputs enabled.\n";
  if (options.mode != "handshake") {
    for (const auto& channel : board->channels()) {
      if (channel.index() == static_cast<uint32_t>(options.channel))
        std::cout << "Channel config: " << channel.ShortDebugString() << '\n';
    }
  }
  if (options.target_steps) std::cout << "target_steps=" << *options.target_steps << '\n';
  if (FLAGS_dry_run) {
    std::cout << "DRY RUN: no hardware opened.\n";
    return absl::OkStatus();
  }
  if (!FLAGS_confirm_hardware)
    return absl::FailedPreconditionError(
        "Review setup, then explicitly pass --confirm_hardware. No port opened.");
  if (interrupted) return absl::CancelledError("Interrupted before opening the port.");
  std::cout << "Opening selected serial port..." << std::endl;
  ABSL_ASSIGN_OR_RETURN(auto comm, robot::comm::CommFactory::CreateComm(board->comm()));
  ABSL_ASSIGN_OR_RETURN(auto transport,
                        robot::comm::GetCommTransport<robot::comm::MessageTransport>(comm));
  for (int elapsed = 0; elapsed < options.settle_ms && !interrupted; elapsed += 10) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return RunSerialV2Validation(
      *board, options, std::move(transport), std::cout, [] { return interrupted != 0; });
}
}  // namespace

int main(int argc, char** argv) {
  gflags::SetUsageMessage(
      "Manual JoshuaWire v2 serial probe. Default: reset/identify/ESTOP only. Use --dry_run "
      "first.");
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  if (argc != 1) {
    std::cerr << "Unexpected positional arguments.\n";
    return 2;
  }
  std::signal(SIGINT, Interrupt);
  std::signal(SIGTERM, Interrupt);
  try {
    const auto status = Run();
    if (!status.ok()) {
      std::cerr << status << '\n';
      return 1;
    }
  } catch (const std::exception& e) {
    std::cerr << "Probe failed: " << e.what() << "; hardware state may be unknown.\n";
    return 1;
  }
  return 0;
}
