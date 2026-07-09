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
#include "robot/action/motors/drivers/am243_ethercat_driver.h"
#include "robot/action/motors/drivers/am243_pdo_codec.h"
#include "robot/action/proto/action_packet.pb.h"
#include "robot/comm/ethercat/ethercat_transport.h"
#include "robot/comm/ethercat/soem_ethercat_transport.h"

namespace {

using robot::comm::ethercat::PdoRegion;
using robot::comm::ethercat::ProcessDataMode;
using robot::comm::ethercat::SlaveIdentity;
using robot::comm::ethercat::SoemEthercatTransport;

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

void PrintSlave(uint16_t index, const SlaveIdentity& slave) {
  std::cout << "slave " << index << ": name=\"" << slave.name << "\" man=0x" << std::hex
            << slave.manufacturer << " id=0x" << slave.product_id << " rev=0x" << slave.revision
            << std::dec << " outputs=" << slave.output_size_bits
            << " bits inputs=" << slave.input_size_bits << " bits\n";
}

absl::StatusOr<robot::action::Actuator> FindAm243Actuator(const config::Config& config) {
  for (const auto& single_action : config.robot().actions().single_actions()) {
    if (single_action.action_type() != robot::action::ActionType::ACTUATOR ||
        !single_action.has_actuator()) {
      continue;
    }
    const auto& actuator = single_action.actuator();
    if (actuator.actuator_type() == robot::action::ActuatorType::AM243_ETHERCAT_ACTUATOR) {
      return actuator;
    }
  }
  return absl::Status(absl::StatusCode::kNotFound,
                      "config does not contain an AM243 EtherCAT actuator");
}

ProcessDataMode ToProcessDataMode(robot::comm::EthercatProcessDataMode mode) {
  if (mode == robot::comm::EthercatProcessDataMode::ETHERCAT_PROCESS_DATA_MODE_LRW) {
    return ProcessDataMode::kLrw;
  }
  return ProcessDataMode::kSplitLrdLwr;
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

  absl::StatusOr<robot::action::Actuator> actuator_or = FindAm243Actuator(*config_or);
  if (!actuator_or.ok()) {
    return Fail(actuator_or.status());
  }
  robot::action::Actuator actuator = *actuator_or;
  if (!interface_override.empty() && interface_override != "-") {
    actuator.mutable_comm()->mutable_ethercat_config()->set_interface_name(interface_override);
  }
  if (slave_index_override > 0) {
    actuator.mutable_am243_ethercat_config()->set_slave_index(
        static_cast<uint32_t>(slave_index_override));
  }

  const auto& ethercat_config = actuator.comm().ethercat_config();
  auto transport = std::make_shared<SoemEthercatTransport>();
  absl::Status status = transport->Init(ethercat_config.interface_name(),
                                        ToProcessDataMode(ethercat_config.process_data_mode()));
  if (!status.ok()) {
    return Fail(status);
  }
  status = transport->ConfigureSlaves();
  if (!status.ok()) {
    return Fail(status);
  }

  absl::StatusOr<std::vector<SlaveIdentity>> slaves_or = transport->GetSlaves();
  if (!slaves_or.ok()) {
    return Fail(slaves_or.status());
  }
  for (size_t i = 0; i < slaves_or->size(); ++i) {
    PrintSlave(static_cast<uint16_t>(i + 1), (*slaves_or)[i]);
  }

  const uint16_t slave_index =
      static_cast<uint16_t>(actuator.am243_ethercat_config().slave_index());
  absl::StatusOr<PdoRegion> region_or = transport->GetPdoRegion(slave_index);
  if (!region_or.ok()) {
    return Fail(region_or.status());
  }

  status = transport->StartCyclic();
  if (!status.ok()) {
    return Fail(status);
  }

  robot::action::Am243EthercatDriver driver(transport, actuator);
  status = driver.Init();
  if (!status.ok()) {
    return Fail(status);
  }

  std::deque<std::string> recent_cycles;
  for (int cycle = 0; cycle < cycles; ++cycle) {
    const float position = PositionForCycle(actuator, cycle);
    robot::action::ActionPacket packet;
    packet.set_action_id("am243_config_smoke");
    packet.set_position(position);

    status = driver.SetAction(packet);
    if (!status.ok()) {
      return Fail(status);
    }

    auto inputs_or = transport->ReadInputs(*region_or);
    if (!inputs_or.ok()) {
      return Fail(inputs_or.status());
    }
    const std::vector<uint8_t> outputs =
        robot::action::am243::EncodeDemoOutputSeed(DemoSeedForPosition(actuator, position));

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
  status = transport->StopCyclic();
  if (!status.ok()) {
    return Fail(status);
  }
  status = transport->Teardown();
  if (!status.ok()) {
    return Fail(status);
  }
  return 0;
}
