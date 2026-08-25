// Board-level smoke for ArduinoBoard. Default is IDENTIFY-only: Init()
// (IDENTIFY handshake + CONFIGURE_CHANNEL). No Enable/SetTarget, no motor
// required. Pass --move later to exercise the STEP/DIR command path.
//
// Usage:
//   bazel run //robot/board/arduino:arduino_driver_smoke -- [port]
//   bazel run //robot/board/arduino:arduino_driver_smoke -- [port] --move
//
// Official Uno R3 typically enumerates as /dev/ttyACM0; CH340 clones often
// as /dev/ttyUSB0. Init() waits ~2s after open for DTR auto-reset.
#include <glog/logging.h>

#include <cstdio>
#include <string>
#include <thread>

#include "robot/board/arduino/arduino_board.h"
#include "robot/board/proto/board.pb.h"
#include "robot/comm/proto/comm.pb.h"

int main(int argc, char** argv) {
  google::InitGoogleLogging(argv[0]);
  std::string port = "/dev/ttyACM0";
  bool move = false;
  for (int i = 1; i < argc; i++) {
    const std::string arg = argv[i];
    if (arg == "--move") {
      move = true;
    } else if (!arg.empty() && arg[0] != '-') {
      port = arg;
    }
  }

  robot::board::Board config;
  config.set_name("stepper_bus");
  config.set_board_type(robot::board::BoardType::ARDUINO_UNO);
  auto* comm = config.mutable_comm();
  comm->set_comm_type(robot::comm::CommType::SERIAL);
  comm->mutable_serial_config()->set_port(port);
  comm->mutable_serial_config()->set_baudrate(115200);
  config.mutable_firmware()->set_min_proto_version(1);
  auto* channel = config.add_channels();
  channel->set_index(0);
  channel->set_drive(robot::board::DriveInterface::STEP_DIR);
  channel->mutable_step_dir()->set_max_pulse_rate_hz(1000);
  channel->mutable_step_dir()->set_enable_active_low(true);
  channel->mutable_step_dir()->set_step_pin(2);
  channel->mutable_step_dir()->set_dir_pin(3);
  channel->mutable_step_dir()->set_enable_pin(4);

  robot::board::ArduinoBoard board;
  auto init_status = board.Init(config);
  printf("Init: %s\n", init_status.ToString().c_str());
  if (!init_status.ok()) return 1;
  printf("IDENTIFY + CONFIGURE_CHANNEL succeeded on %s\n", port.c_str());
  if (!move) {
    return 0;
  }

  auto channel_or = board.OpenChannel(0);
  if (!channel_or.ok()) {
    printf("OpenChannel: %s\n", channel_or.status().ToString().c_str());
    return 1;
  }
  auto ch = *channel_or;

  auto enable_status = ch->Enable();
  printf("Enable: %s\n", enable_status.ToString().c_str());
  if (!enable_status.ok()) return 1;

  for (int i = 0; i < 5; i++) {
    float target = (i % 2 == 0) ? 500.0f : -500.0f;
    auto status = ch->SetTarget(robot::board::TargetMode::kPosition, target);
    printf("SetTarget[%d] target=%.1f: %s\n", i, target, status.ToString().c_str());
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
  }
  return 0;
}
