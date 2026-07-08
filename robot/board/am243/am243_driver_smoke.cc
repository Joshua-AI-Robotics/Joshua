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
#include "config/proto/robot.pb.h"
#include "robot/action/motors/drivers/joint_driver.h"
#include "robot/action/proto/action_packet.pb.h"
#include "robot/board/am243/am243_board.h"
#include "robot/board/am243/am243_pdo_codec.h"
#include "robot/board/proto/board.pb.h"
#include "robot/comm/ethercat/ethercat_transport.h"
#include "robot/comm/factory/comm_factory.h"

namespace {

using robot::comm::ethercat::PdoRegion;

constexpr float kLowerLimit = -90.0f;
constexpr float kUpperLimit = 90.0f;
constexpr size_t kDisplayedCycles = 5;

void PrintUsage(const char* argv0) {
  std::cerr << "usage: " << argv0 << " <interface> [cycles] [period_us] [slave_index]\n"
            << "example: " << argv0 << " enp5s0 80 5000 1\n";
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

robot::board::Board MakeBoardConfig(const std::string& interface_name, uint16_t slave_index) {
  robot::board::Board board;
  board.set_name("am243_smoke");
  board.set_board_type(robot::board::BoardType::AM243);

  auto* comm = board.mutable_comm();
  comm->set_comm_type(robot::comm::CommType::ETHERCAT);
  auto* ethercat_config = comm->mutable_ethercat_config();
  ethercat_config->set_interface_name(interface_name);
  ethercat_config->set_process_data_mode(
      robot::comm::EthercatProcessDataMode::ETHERCAT_PROCESS_DATA_MODE_SPLIT_LRD_LWR);

  auto* channel = board.add_channels();
  channel->set_index(0);
  channel->set_drive(robot::board::DriveInterface::PDO_JOINT);

  auto* am243_config = board.mutable_am243_config();
  am243_config->set_slave_index(slave_index);
  am243_config->set_pdo_mapping(robot::board::Am243PdoMapping::AM243_PDO_MAPPING_TI_DEMO);
  return board;
}

robot::action::Actuator MakeActuatorConfig() {
  robot::action::Actuator actuator;
  actuator.set_actuator_name("am243_demo");
  actuator.set_id(1);
  actuator.set_motor_type(robot::action::MotorType::MOTOR_GENERIC_JOINT);
  actuator.set_board_name("am243_smoke");
  actuator.set_channel(0);
  actuator.set_physical_lower_limit(kLowerLimit);
  actuator.set_physical_upper_limit(kUpperLimit);
  actuator.set_operational_lower_limit(kLowerLimit);
  actuator.set_operational_upper_limit(kUpperLimit);
  return actuator;
}

float PositionForCycle(int cycle) {
  const int seed = cycle & 0xff;
  return kLowerLimit + ((kUpperLimit - kLowerLimit) * static_cast<float>(seed) / 255.0f);
}

uint8_t DemoSeedForPosition(float position) {
  const float normalized = (position - kLowerLimit) / (kUpperLimit - kLowerLimit);
  return static_cast<uint8_t>(std::lround(std::clamp(normalized * 255.0f, 0.0f, 255.0f)));
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2 || argc > 5) {
    PrintUsage(argv[0]);
    return 2;
  }

  const std::string interface_name = argv[1];
  const int cycles = argc >= 3 ? ParsePositiveInt(argv[2], 80) : 80;
  const int period_us = argc >= 4 ? ParsePositiveInt(argv[3], 5000) : 5000;
  const uint16_t slave_index = static_cast<uint16_t>(argc >= 5 ? ParsePositiveInt(argv[4], 1) : 1);

  const robot::board::Board board_config = MakeBoardConfig(interface_name, slave_index);

  // Board init owns the full SOEM bring-up: open master, ConfigureSlaves,
  // StartCyclic, verify OPERATIONAL, resolve the PDO region.
  robot::board::Am243Board board;
  absl::Status status = board.Init(board_config);
  if (!status.ok()) {
    return Fail(status);
  }

  auto channel_or = board.OpenChannel(0);
  if (!channel_or.ok()) {
    return Fail(channel_or.status());
  }

  // The CommFactory cache hands back the same master the board opened, so
  // the smoke can print the raw slave I/O next to the driver commands.
  auto transport_or = robot::comm::CommFactory::CreateEthercatTransport(board_config.comm());
  if (!transport_or.ok()) {
    return Fail(transport_or.status());
  }
  auto region_or = (*transport_or)->GetPdoRegion(slave_index);
  if (!region_or.ok()) {
    return Fail(region_or.status());
  }

  robot::action::JointDriver driver(*channel_or, MakeActuatorConfig());
  status = driver.Init();
  if (!status.ok()) {
    return Fail(status);
  }

  std::deque<std::string> recent_cycles;
  for (int cycle = 0; cycle < cycles; ++cycle) {
    const float position = PositionForCycle(cycle);
    robot::action::ActionPacket packet;
    packet.set_action_id("am243_driver_smoke");
    packet.set_position(position);

    status = driver.SetAction(packet);
    if (!status.ok()) {
      return Fail(status);
    }

    auto inputs_or = (*transport_or)->ReadInputs(*region_or);
    if (!inputs_or.ok()) {
      return Fail(inputs_or.status());
    }
    const std::vector<uint8_t> outputs =
        robot::board::am243::EncodeDemoOutputSeed(DemoSeedForPosition(position));

    std::ostringstream cycle_line;
    cycle_line << "cycle=" << cycle << " position=" << position << " O=[" << FormatBytes(outputs)
               << "] I=[" << FormatBytes(*inputs_or) << "]";
    ShowRecentCycles(&recent_cycles, cycle_line.str());

    if (cycle + 1 < cycles) {
      std::this_thread::sleep_for(std::chrono::microseconds(period_us));
    }
  }

  status = driver.Teardown();
  if (!status.ok()) {
    return Fail(status);
  }
  status = board.Teardown();
  if (!status.ok()) {
    return Fail(status);
  }
  return 0;
}
