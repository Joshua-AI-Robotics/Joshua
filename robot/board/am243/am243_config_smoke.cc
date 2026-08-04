#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "config/config_utils.h"
#include "config/proto/config.pb.h"
#include "robot/action/factory/action_factory.h"
#include "robot/action/proto/action_packet.pb.h"
#include "robot/board/am243/am243_pdo_codec.h"
#include "robot/comm/ethercat/ethercat_transport.h"
#include "robot/comm/factory/comm_factory.h"

// Proves the config-driven AM243 path end to end: pbtxt -> ActionFactory ->
// BoardFactory -> Am243Board -> shared SOEM master, the same resolution the
// actuator_subscriber node runs (docs/BOARD_LAYER_RFC.md §6.5).

namespace {

using robot::comm::ethercat::PdoRegion;

constexpr size_t kDisplayedCycles = 5;

void PrintUsage(const char* argv0) {
  std::cerr << "usage: " << argv0
            << " <config_pbtxt> [interface_override] [cycles] [period_us] [slave_index_override]\n"
            << "example: " << argv0
            << " config/config_preset/example/am243_ethercat_demo.pbtxt enp5s0 80 5000 1\n";
}

int ParsePositiveInt(const char* value, int fallback) {
  char* end = nullptr;
  const long parsed = std::strtol(value, &end, 10);
  if (end == value || *end != '\0' || parsed <= 0) {
    return fallback;
  }
  return static_cast<int>(parsed);
}

int Fail(const absl::Status& status) {
  std::cerr << "error: " << status << "\n";
  return 1;
}

std::string FormatBytes(const std::vector<uint8_t>& bytes) {
  std::ostringstream formatted;
  for (size_t i = 0; i < bytes.size(); ++i) {
    if (i > 0) {
      formatted << ' ';
    }
    formatted << std::uppercase << std::hex << std::setw(2) << std::setfill('0')
              << static_cast<int>(bytes[i]);
  }
  return formatted.str();
}

void ShowRecentCycles(std::deque<std::string>* recent_cycles, const std::string& cycle_line) {
  recent_cycles->push_back(cycle_line);
  while (recent_cycles->size() > kDisplayedCycles) {
    recent_cycles->pop_front();
  }

  std::cout << "\033[2J\033[H";
  std::cout << "showing last " << recent_cycles->size() << " cycles\n";
  for (const auto& line : *recent_cycles) {
    std::cout << line << "\n";
  }
  std::cout.flush();
}

absl::StatusOr<robot::action::SingleAction> FindJointSingleAction(const config::Config& config) {
  for (const auto& single_action : config.robot().actions().single_actions()) {
    if (single_action.action_type() != robot::action::ActionType::ACTUATOR ||
        !single_action.has_actuator()) {
      continue;
    }
    if (single_action.actuator().motor_type() == robot::action::MotorType::MOTOR_TI_DEMO) {
      return single_action;
    }
  }
  return absl::Status(absl::StatusCode::kNotFound,
                      "config does not contain a MOTOR_TI_DEMO actuator");
}

float PositionForCycle(const robot::action::Actuator& actuator, int cycle) {
  const float lower = actuator.operational_lower_limit();
  const float upper = actuator.operational_upper_limit();
  const int seed = cycle & 0xff;
  return lower + ((upper - lower) * static_cast<float>(seed) / 255.0f);
}

uint8_t DemoSeedForPosition(const robot::action::Actuator& actuator, float position) {
  const float lower = actuator.operational_lower_limit();
  const float upper = actuator.operational_upper_limit();
  const float normalized = (position - lower) / (upper - lower);
  return static_cast<uint8_t>(std::lround(std::clamp(normalized * 255.0f, 0.0f, 255.0f)));
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2 || argc > 6) {
    PrintUsage(argv[0]);
    return 2;
  }

  const std::string config_path = argv[1];
  const std::string interface_override = argc >= 3 ? argv[2] : "";
  const int cycles = argc >= 4 ? ParsePositiveInt(argv[3], 80) : 80;
  const int period_us = argc >= 5 ? ParsePositiveInt(argv[4], 5000) : 5000;
  const int slave_index_override = argc >= 6 ? ParsePositiveInt(argv[5], 0) : 0;

  absl::StatusOr<config::Config> config_or = config::config_util::LoadConfig(config_path);
  if (!config_or.ok()) {
    return Fail(config_or.status());
  }
  config::Config config = *config_or;

  absl::StatusOr<robot::action::SingleAction> single_action_or = FindJointSingleAction(config);
  if (!single_action_or.ok()) {
    return Fail(single_action_or.status());
  }
  const robot::action::Actuator& actuator = single_action_or->actuator();

  auto* boards = config.mutable_robot()->mutable_boards();
  robot::board::Board* board_config = nullptr;
  for (auto& board : *boards) {
    if (board.name() == actuator.board_name()) {
      board_config = &board;
      break;
    }
  }
  if (board_config == nullptr) {
    return Fail(absl::Status(absl::StatusCode::kNotFound,
                             "config declares no board named " + actuator.board_name()));
  }
  if (!interface_override.empty() && interface_override != "-") {
    board_config->mutable_comm()->mutable_ethercat_config()->set_interface_name(interface_override);
  }
  if (slave_index_override > 0) {
    board_config->mutable_am243_config()->set_slave_index(
        static_cast<uint32_t>(slave_index_override));
  }

  auto interface_or =
      robot::action::ActionFactory::CreateAction(*single_action_or, config.robot().boards());
  if (!interface_or.ok()) {
    return Fail(interface_or.status());
  }
  auto driver = std::move(*interface_or);

  // Same cached master the board opened, for printing raw slave I/O.
  auto transport_or = robot::comm::CommFactory::CreateEthercatTransport(board_config->comm());
  if (!transport_or.ok()) {
    return Fail(transport_or.status());
  }
  auto region_or =
      (*transport_or)
          ->GetPdoRegion(static_cast<uint16_t>(board_config->am243_config().slave_index()));
  if (!region_or.ok()) {
    return Fail(region_or.status());
  }

  std::deque<std::string> recent_cycles;
  for (int cycle = 0; cycle < cycles; ++cycle) {
    const float position = PositionForCycle(actuator, cycle);
    robot::action::ActionPacket packet;
    packet.set_action_id("am243_config_smoke");
    packet.set_position(position);

    absl::Status status = driver->SetAction(packet);
    if (!status.ok()) {
      return Fail(status);
    }

    auto inputs_or = (*transport_or)->ReadInputs(*region_or);
    if (!inputs_or.ok()) {
      return Fail(inputs_or.status());
    }
    const std::vector<uint8_t> outputs =
        robot::board::am243::EncodeDemoOutputSeed(DemoSeedForPosition(actuator, position));

    std::ostringstream cycle_line;
    cycle_line << "cycle=" << cycle << " position=" << position << " O=[" << FormatBytes(outputs)
               << "] I=[" << FormatBytes(*inputs_or) << "]";
    ShowRecentCycles(&recent_cycles, cycle_line.str());

    if (cycle + 1 < cycles) {
      std::this_thread::sleep_for(std::chrono::microseconds(period_us));
    }
  }

  robot::action::ActionPacket teardown_packet;
  teardown_packet.set_preset(robot::action::PresetCommand::PRESET_TEARDOWN);
  absl::Status status = driver->SetAction(teardown_packet);
  if (!status.ok()) {
    return Fail(status);
  }
  return 0;
}
