#include <chrono>
#include <cstdint>
#include <cstdlib>
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
#include "robot/action/motors/drivers/am243_ethercat_driver.h"
#include "robot/action/proto/action_packet.pb.h"
#include "robot/comm/ethercat/ethercat_status.h"
#include "robot/comm/ethercat/ethercat_transport.h"
#include "robot/comm/ethercat/soem_ethercat_transport.h"

namespace {

using robot::comm::ethercat::PdoRegion;
using robot::comm::ethercat::ProcessDataMode;
using robot::comm::ethercat::SlaveIdentity;
using robot::comm::ethercat::SoemEthercatTransport;

constexpr float kLowerLimit = -90.0f;
constexpr float kUpperLimit = 90.0f;

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

void PrintSlave(uint16_t index, const SlaveIdentity& slave) {
  std::cout << "slave " << index << ": name=\"" << slave.name << "\" man=0x" << std::hex
            << slave.manufacturer << " id=0x" << slave.product_id << " rev=0x" << slave.revision
            << std::dec << " outputs=" << slave.output_size_bits
            << " bits inputs=" << slave.input_size_bits << " bits\n";
}

robot::action::Actuator MakeActuatorConfig(const std::string& interface_name,
                                           const PdoRegion& region) {
  robot::action::Actuator actuator;
  actuator.set_actuator_name("am243_demo");
  actuator.set_id(1);
  actuator.set_actuator_type(robot::action::ActuatorType::AM243_ETHERCAT_ACTUATOR);
  actuator.set_physical_lower_limit(kLowerLimit);
  actuator.set_physical_upper_limit(kUpperLimit);
  actuator.set_operational_lower_limit(kLowerLimit);
  actuator.set_operational_upper_limit(kUpperLimit);

  auto* comm = actuator.mutable_comm();
  comm->set_comm_type(robot::comm::CommType::ETHERCAT);
  auto* ethercat_config = comm->mutable_ethercat_config();
  ethercat_config->set_interface_name(interface_name);
  ethercat_config->set_process_data_mode(
      robot::comm::EthercatProcessDataMode::ETHERCAT_PROCESS_DATA_MODE_SPLIT_LRD_LWR);

  auto* am243_config = actuator.mutable_am243_ethercat_config();
  am243_config->set_slave_index(region.slave_index);
  am243_config->set_output_offset_bytes(region.output_offset_bytes);
  am243_config->set_input_offset_bytes(region.input_offset_bytes);
  am243_config->set_output_size_bytes(region.output_size_bytes);
  am243_config->set_input_size_bytes(region.input_size_bytes);
  am243_config->set_idle_position(0.0f);
  am243_config->set_pdo_mapping(robot::action::Am243PdoMapping::AM243_PDO_MAPPING_TI_DEMO);
  return actuator;
}

float PositionForCycle(int cycle) {
  const int seed = cycle & 0xff;
  return kLowerLimit + ((kUpperLimit - kLowerLimit) * static_cast<float>(seed) / 255.0f);
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

  auto transport = std::make_shared<SoemEthercatTransport>();
  absl::Status status = transport->Init(interface_name, ProcessDataMode::kSplitLrdLwr);
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

  absl::StatusOr<PdoRegion> region_or = transport->GetPdoRegion(slave_index);
  if (!region_or.ok()) {
    return Fail(region_or.status());
  }

  status = transport->StartCyclic();
  if (!status.ok()) {
    return Fail(status);
  }

  robot::action::Am243EthercatDriver driver(transport,
                                            MakeActuatorConfig(interface_name, *region_or));
  status = driver.Init();
  if (!status.ok()) {
    return Fail(status);
  }

  for (int cycle = 0; cycle < cycles; ++cycle) {
    const float position = PositionForCycle(cycle);
    robot::action::ActionPacket packet;
    packet.set_action_id("am243_driver_smoke");
    packet.set_position(position);

    status = driver.SetAction(packet);
    if (!status.ok()) {
      return Fail(status);
    }

    auto inputs_or = transport->ReadInputs(*region_or);
    if (!inputs_or.ok()) {
      return Fail(inputs_or.status());
    }

    std::cout << "cycle=" << cycle << " position=" << position << " I=[" << FormatBytes(*inputs_or)
              << "]\n";

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
