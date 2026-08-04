#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "robot/board/am243/am243_pdo_codec.h"
#include "robot/comm/ethercat/ethercat_status.h"
#include "robot/comm/ethercat/ethercat_transport.h"
#include "robot/comm/ethercat/soem_ethercat_transport.h"

namespace {

using robot::board::am243::DecodeDemoInputEchoSeed;
using robot::board::am243::EncodeDemoOutputSeed;
using robot::board::am243::kDemoPdoSizeBytes;
using robot::comm::ethercat::PdoRegion;
using robot::comm::ethercat::ProcessDataMode;
using robot::comm::ethercat::SlaveIdentity;
using robot::comm::ethercat::SoemEthercatTransport;
using robot::comm::ethercat::ValidateWorkingCount;

constexpr size_t kDisplayedCycles = 5;

void PrintUsage(const char* argv0) {
  std::cerr << "usage: " << argv0 << " <interface> [cycles] [slave_index] [period_us]\n"
            << "example: " << argv0 << " enp5s0 20 1 5000\n";
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
  std::cout << "slave " << index << ": name=\"" << slave.name << "\""
            << " man=0x" << std::hex << slave.manufacturer << " id=0x" << slave.product_id
            << " rev=0x" << slave.revision << std::dec << " outputs=" << slave.output_size_bits
            << " bits inputs=" << slave.input_size_bits << " bits\n";
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2 || argc > 5) {
    PrintUsage(argv[0]);
    return 2;
  }

  const std::string interface_name = argv[1];
  const int cycles = argc >= 3 ? ParsePositiveInt(argv[2], 20) : 20;
  const uint16_t slave_index = static_cast<uint16_t>(argc >= 4 ? ParsePositiveInt(argv[3], 1) : 1);
  const int period_us = argc >= 5 ? ParsePositiveInt(argv[4], 5000) : 5000;

  SoemEthercatTransport transport;
  absl::Status status = transport.Init(interface_name, ProcessDataMode::kSplitLrdLwr);
  if (!status.ok()) {
    return Fail(status);
  }

  status = transport.ConfigureSlaves();
  if (!status.ok()) {
    return Fail(status);
  }

  absl::StatusOr<std::vector<SlaveIdentity>> slaves_or = transport.GetSlaves();
  if (!slaves_or.ok()) {
    return Fail(slaves_or.status());
  }
  for (size_t i = 0; i < slaves_or->size(); ++i) {
    PrintSlave(static_cast<uint16_t>(i + 1), (*slaves_or)[i]);
  }

  absl::StatusOr<PdoRegion> region_or = transport.GetPdoRegion(slave_index);
  if (!region_or.ok()) {
    return Fail(region_or.status());
  }
  const PdoRegion region = *region_or;
  if (region.output_size_bytes != kDemoPdoSizeBytes ||
      region.input_size_bytes != kDemoPdoSizeBytes) {
    std::cerr << "error: AM243 demo PDO region must be 8 output bytes and 8 input bytes; got "
              << region.output_size_bytes << " output and " << region.input_size_bytes
              << " input bytes\n";
    return 1;
  }

  status = transport.StartCyclic();
  if (!status.ok()) {
    return Fail(status);
  }

  std::deque<std::string> recent_cycles;
  for (int cycle = 0; cycle < cycles; ++cycle) {
    const uint8_t seed = static_cast<uint8_t>(cycle & 0xff);
    const std::vector<uint8_t> outputs = EncodeDemoOutputSeed(seed);
    status = transport.WriteOutputs(region, outputs);
    if (!status.ok()) {
      return Fail(status);
    }

    auto process_data_or = transport.ExchangeProcessData();
    if (!process_data_or.ok()) {
      return Fail(process_data_or.status());
    }

    auto inputs_or = transport.ReadInputs(region);
    if (!inputs_or.ok()) {
      return Fail(inputs_or.status());
    }

    auto echo_or = DecodeDemoInputEchoSeed(*inputs_or);
    if (!echo_or.ok()) {
      return Fail(echo_or.status());
    }

    const absl::Status wkc_status = ValidateWorkingCount(process_data_or->working_count,
                                                         process_data_or->expected_working_count);
    std::ostringstream cycle_line;
    cycle_line << "cycle=" << cycle << " seed=" << static_cast<int>(seed)
               << " echo=" << static_cast<int>(*echo_or)
               << " wkc=" << process_data_or->working_count << "/"
               << process_data_or->expected_working_count << " O=[" << FormatBytes(outputs)
               << "] I=[" << FormatBytes(*inputs_or) << "]";
    if (!wkc_status.ok()) {
      cycle_line << " " << wkc_status;
    }
    ShowRecentCycles(&recent_cycles, cycle_line.str());
    if (cycle + 1 < cycles) {
      std::this_thread::sleep_for(std::chrono::microseconds(period_us));
    }
  }

  status = transport.StopCyclic();
  if (!status.ok()) {
    return Fail(status);
  }
  status = transport.Teardown();
  if (!status.ok()) {
    return Fail(status);
  }
  return 0;
}
