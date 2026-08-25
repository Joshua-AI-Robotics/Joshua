// Config-driven AM243 smoke for either serial joshua_wire_v1 or the retained
// EtherCAT TI demo: pbtxt -> ActionFactory -> BoardFactory -> Am243Board.
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "config/config_utils.h"
#include "config/proto/config.pb.h"
#include "robot/action/factory/action_factory.h"
#include "robot/action/proto/action_packet.pb.h"

namespace {

void PrintUsage(const char* argv0) {
  std::cerr << "usage: " << argv0 << " <config_pbtxt> [comm_override] [cycles]\n"
            << "example: " << argv0
            << " config/config_preset/example/am243_serial_demo.pbtxt /dev/ttyACM0 5\n"
            << "example: " << argv0
            << " config/config_preset/example/am243_ethercat_demo.pbtxt enp5s0 5\n";
}

int Fail(const absl::Status& status) {
  std::cerr << "error: " << status << "\n";
  return 1;
}

int ParsePositiveInt(const char* value, int fallback) {
  char* end = nullptr;
  const long parsed = std::strtol(value, &end, 10);
  return end != value && *end == '\0' && parsed > 0 ? static_cast<int>(parsed) : fallback;
}

absl::StatusOr<robot::action::SingleAction> FindAm243Action(const config::Config& config) {
  for (const auto& single_action : config.robot().actions().single_actions()) {
    if (!single_action.has_actuator()) {
      continue;
    }
    const auto motor_type = single_action.actuator().motor_type();
    if (motor_type == robot::action::MotorType::MOTOR_STEPPER_NEMA17 ||
        motor_type == robot::action::MotorType::MOTOR_TI_DEMO) {
      return single_action;
    }
  }
  return absl::NotFoundError("config does not contain an AM243 demo actuator");
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2 || argc > 4) {
    PrintUsage(argv[0]);
    return 2;
  }

  auto config_or = config::config_util::LoadConfig(argv[1]);
  if (!config_or.ok()) return Fail(config_or.status());
  config::Config config = *config_or;

  auto single_action_or = FindAm243Action(config);
  if (!single_action_or.ok()) return Fail(single_action_or.status());
  robot::action::SingleAction single_action = *single_action_or;

  if (argc >= 3 && std::string(argv[2]) != "-") {
    for (auto& board : *config.mutable_robot()->mutable_boards()) {
      if (board.name() == single_action.actuator().board_name()) {
        if (board.comm().comm_type() == robot::comm::CommType::SERIAL) {
          board.mutable_comm()->mutable_serial_config()->set_port(argv[2]);
        } else if (board.comm().comm_type() == robot::comm::CommType::ETHERCAT) {
          board.mutable_comm()->mutable_ethercat_config()->set_interface_name(argv[2]);
        }
      }
    }
  }

  auto action_or =
      robot::action::ActionFactory::CreateAction(single_action, config.robot().boards());
  if (!action_or.ok()) return Fail(action_or.status());
  auto action = std::move(*action_or);
  const int cycles = argc >= 4 ? ParsePositiveInt(argv[3], 5) : 5;

  for (int cycle = 0; cycle < cycles; ++cycle) {
    robot::action::ActionPacket packet;
    packet.set_action_id("am243_config_smoke");
    packet.set_position(cycle % 2 == 0 ? 90.0f : -90.0f);
    const absl::Status status = action->SetAction(packet);
    std::cout << "cycle=" << cycle << " position=" << packet.position() << " " << status << "\n";
    if (!status.ok()) return Fail(status);
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
  }

  robot::action::ActionPacket teardown;
  teardown.set_preset(robot::action::PresetCommand::PRESET_TEARDOWN);
  return action->SetAction(teardown).ok() ? 0 : 1;
}
