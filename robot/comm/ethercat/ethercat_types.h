// Comm-internal bus metadata and process-data snapshots. No board-facing API.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace robot::comm::ethercat {

enum class ProcessDataMode {
  kSplitLrdLwr,
  kLrw,
};

struct SlaveIdentity {
  std::string name;
  uint32_t manufacturer = 0;
  uint32_t product_id = 0;
  uint32_t revision = 0;
  uint16_t output_size_bits = 0;
  uint16_t input_size_bits = 0;
};

struct PdoRegion {
  uint16_t slave_index = 0;
  size_t output_offset_bytes = 0;
  size_t input_offset_bytes = 0;
  size_t output_size_bytes = 0;
  size_t input_size_bytes = 0;
};

struct ProcessData {
  std::vector<uint8_t> outputs;
  std::vector<uint8_t> inputs;
  int expected_working_count = 0;
  int working_count = 0;
};

}  // namespace robot::comm::ethercat
