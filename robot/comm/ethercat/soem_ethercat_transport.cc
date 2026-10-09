#include "robot/comm/ethercat/soem_ethercat_transport.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "soem/soem.h"

namespace robot::comm::ethercat {

namespace {

absl::Status NotInitializedStatus(const std::string& operation) {
  return absl::Status(absl::StatusCode::kFailedPrecondition,
                      "SOEM EtherCAT transport must be initialized before " + operation);
}

absl::Status NotConfiguredStatus(const std::string& operation) {
  return absl::Status(absl::StatusCode::kFailedPrecondition,
                      "SOEM EtherCAT slaves must be configured before " + operation);
}

absl::Status NotStartedStatus(const std::string& operation) {
  return absl::Status(absl::StatusCode::kFailedPrecondition,
                      "SOEM EtherCAT cyclic exchange must be started before " + operation);
}

size_t ByteOffset(const uint8_t* ptr, const uint8_t* base) {
  if (ptr == nullptr || base == nullptr || ptr < base) {
    return 0;
  }
  return static_cast<size_t>(ptr - base);
}

int ExpectedWorkingCount(const ec_groupt& group) {
  return static_cast<int>(group.outputsWKC) * 2 + static_cast<int>(group.inputsWKC);
}

absl::Status ValidateRegionBounds(const PdoRegion& region, const ec_groupt& group) {
  if (region.output_offset_bytes > group.Obytes ||
      region.output_size_bytes > group.Obytes - region.output_offset_bytes) {
    return absl::Status(absl::StatusCode::kInvalidArgument,
                        "SOEM EtherCAT output PDO region is outside mapped outputs");
  }
  if (region.input_offset_bytes > group.Ibytes ||
      region.input_size_bytes > group.Ibytes - region.input_offset_bytes) {
    return absl::Status(absl::StatusCode::kInvalidArgument,
                        "SOEM EtherCAT input PDO region is outside mapped inputs");
  }
  return absl::OkStatus();
}

std::string Hex32(uint32_t value) {
  std::ostringstream hex;
  hex << "0x" << std::hex << std::setw(8) << std::setfill('0') << value;
  return hex.str();
}

std::string Hex16(uint16_t value) {
  std::ostringstream hex;
  hex << "0x" << std::hex << std::setw(4) << std::setfill('0') << value;
  return hex.str();
}

std::string BuildMappingFailureMessage(ecx_contextt* context, bool zero_identity_retry_attempted) {
  std::ostringstream message;
  message << "SOEM EtherCAT failed to map slave process data"
          << "; zero_identity_retry=" << (zero_identity_retry_attempted ? 1 : 0)
          << " slavecount=" << context->slavecount;
  for (int slave_index = 1; slave_index <= context->slavecount; ++slave_index) {
    ec_slavet& slave = context->slavelist[slave_index];
    const uint16_t al_status =
        ecx_FPRDw(&context->port, slave.configadr, ECT_REG_ALSTAT, EC_TIMEOUTRET);
    const uint16_t pdi_control =
        ecx_FPRDw(&context->port, slave.configadr, ECT_REG_PDICTL, EC_TIMEOUTRET);
    const uint16_t eeprom_status =
        ecx_FPRDw(&context->port, slave.configadr, ECT_REG_EEPSTAT, EC_TIMEOUTRET);
    const uint32_t sii_manufacturer =
        etohl(ecx_readeeprom(context, slave_index, ECT_SII_MANUF, EC_TIMEOUTEEP));
    const uint32_t sii_product =
        etohl(ecx_readeeprom(context, slave_index, ECT_SII_ID, EC_TIMEOUTEEP));
    const uint32_t sii_revision =
        etohl(ecx_readeeprom(context, slave_index, ECT_SII_REV, EC_TIMEOUTEEP));

    message << " slave[" << slave_index << "]"
            << " name=\"" << slave.name << "\""
            << " Obits=" << slave.Obits << " Ibits=" << slave.Ibits << " Obytes=" << slave.Obytes
            << " Ibytes=" << slave.Ibytes << " blockLRW=" << static_cast<int>(slave.blockLRW)
            << " configadr=" << Hex16(slave.configadr) << " al_status=" << Hex16(al_status)
            << " pdi_control=" << Hex16(pdi_control) << " eeprom_status=" << Hex16(eeprom_status)
            << " sii_manufacturer=" << Hex32(sii_manufacturer)
            << " sii_product=" << Hex32(sii_product) << " sii_revision=" << Hex32(sii_revision);
  }
  return message.str();
}

bool LooksLikeZeroIdentity(const ecx_contextt& context) {
  if (context.slavecount <= 0) {
    return false;
  }
  for (int slave_index = 1; slave_index <= context.slavecount; ++slave_index) {
    const ec_slavet& slave = context.slavelist[slave_index];
    if (slave.eep_man != 0 || slave.eep_id != 0 || slave.eep_rev != 0) {
      return false;
    }
  }
  return true;
}

absl::Status ReopenSoemSocket(ecx_contextt* context, const std::string& interface_name) {
  ecx_close(context);
  std::memset(context, 0, sizeof(*context));
  osal_usleep(100000);
  if (!ecx_init(context, interface_name.c_str())) {
    return absl::Status(absl::StatusCode::kUnavailable,
                        "failed to reopen SOEM EtherCAT interface: " + interface_name);
  }
  osal_usleep(100000);
  return absl::OkStatus();
}

}  // namespace

struct SoemEthercatTransport::State {
  ecx_contextt context;
  std::vector<uint8_t> io_map;
  std::vector<SlaveIdentity> slaves;
  std::vector<PdoRegion> pdo_regions;
  bool initialized = false;
  bool configured = false;
  bool cyclic_started = false;
  bool mailbox_closed = false;
};

SoemEthercatTransport::SoemEthercatTransport() : state_(std::make_unique<State>()) {
  std::memset(&state_->context, 0, sizeof(state_->context));
}

SoemEthercatTransport::~SoemEthercatTransport() {
  (void)Teardown();
}

absl::Status SoemEthercatTransport::Init(const std::string& interface_name,
                                         ProcessDataMode process_data_mode) {
  if (interface_name.empty()) {
    return absl::Status(absl::StatusCode::kInvalidArgument,
                        "SOEM EtherCAT interface name is empty");
  }

  if (process_data_mode != ProcessDataMode::kSplitLrdLwr) {
    return absl::Status(absl::StatusCode::kInvalidArgument,
                        "SOEM EtherCAT transport currently requires split LRD/LWR mode");
  }

  if (state_->initialized) {
    return absl::Status(absl::StatusCode::kAlreadyExists,
                        "SOEM EtherCAT transport is already initialized");
  }

  if (!ecx_init(&state_->context, interface_name.c_str())) {
    std::memset(&state_->context, 0, sizeof(state_->context));
    return absl::Status(absl::StatusCode::kUnavailable,
                        "failed to open SOEM EtherCAT interface: " + interface_name);
  }

  interface_name_ = interface_name;
  process_data_mode_ = process_data_mode;
  state_->initialized = true;
  return absl::OkStatus();
}

absl::Status SoemEthercatTransport::ConfigureSlaves() {
  if (!state_->initialized) {
    return NotInitializedStatus("ConfigureSlaves");
  }
  if (state_->configured) {
    return absl::OkStatus();
  }

  ecx_contextt* context = &state_->context;
  if (ecx_config_init(context) <= 0) {
    return absl::Status(absl::StatusCode::kUnavailable,
                        "SOEM EtherCAT did not find any slaves on interface: " + interface_name_);
  }
  bool zero_identity_retry_attempted = false;
  if (LooksLikeZeroIdentity(*context)) {
    zero_identity_retry_attempted = true;
    absl::Status reopen_status = ReopenSoemSocket(context, interface_name_);
    if (!reopen_status.ok()) {
      return reopen_status;
    }
    if (ecx_config_init(context) <= 0) {
      return absl::Status(
          absl::StatusCode::kUnavailable,
          "SOEM EtherCAT did not find any slaves after reopening interface: " + interface_name_);
    }
  }

  for (int slave_index = 1; slave_index <= context->slavecount; ++slave_index) {
    context->slavelist[slave_index].blockLRW = 1;
  }
  context->slavelist[0].blockLRW = static_cast<uint8_t>(context->slavecount);

  constexpr uint8_t kDefaultGroup = 0;
  constexpr size_t kIoMapSize = 4096;
  state_->io_map.assign(kIoMapSize, 0);
  const int io_map_size = ecx_config_map_group(context, state_->io_map.data(), kDefaultGroup);
  if (io_map_size <= 0) {
    return absl::Status(absl::StatusCode::kUnavailable,
                        BuildMappingFailureMessage(context, zero_identity_retry_attempted));
  }

  ecx_configdc(context);

  state_->slaves.clear();
  state_->pdo_regions.clear();
  state_->slaves.reserve(static_cast<size_t>(context->slavecount));
  state_->pdo_regions.reserve(static_cast<size_t>(context->slavecount));

  const ec_groupt& group = context->grouplist[kDefaultGroup];
  for (int slave_index = 1; slave_index <= context->slavecount; ++slave_index) {
    const ec_slavet& slave = context->slavelist[slave_index];

    SlaveIdentity identity;
    identity.name = slave.name;
    identity.manufacturer = slave.eep_man;
    identity.product_id = slave.eep_id;
    identity.revision = slave.eep_rev;
    identity.output_size_bits = slave.Obits;
    identity.input_size_bits = slave.Ibits;
    state_->slaves.push_back(identity);

    PdoRegion region;
    region.slave_index = static_cast<uint16_t>(slave_index);
    region.output_offset_bytes = ByteOffset(slave.outputs, group.outputs);
    region.input_offset_bytes = ByteOffset(slave.inputs, group.inputs);
    region.output_size_bytes = slave.Obytes;
    region.input_size_bytes = slave.Ibytes;
    state_->pdo_regions.push_back(region);
  }

  state_->configured = true;
  return absl::OkStatus();
}

absl::Status SoemEthercatTransport::StartCyclic() {
  if (!state_->configured) {
    return NotConfiguredStatus("StartCyclic");
  }
  if (state_->cyclic_started) {
    return absl::OkStatus();
  }
  state_->mailbox_closed = true;

  constexpr uint8_t kDefaultGroup = 0;
  ecx_contextt* context = &state_->context;
  ec_slavet* broadcast_slave = context->slavelist;

  ecx_statecheck(context, 0, EC_STATE_SAFE_OP, EC_TIMEOUTSTATE * 4);

  ecx_send_processdata_group(context, kDefaultGroup);
  (void)ecx_receive_processdata_group(context, kDefaultGroup, EC_TIMEOUTRET);

  broadcast_slave->state = EC_STATE_OPERATIONAL;
  ecx_writestate(context, 0);

  for (int attempt = 0; attempt < 10; ++attempt) {
    ecx_send_processdata_group(context, kDefaultGroup);
    (void)ecx_receive_processdata_group(context, kDefaultGroup, EC_TIMEOUTRET);
    ecx_statecheck(context, 0, EC_STATE_OPERATIONAL, EC_TIMEOUTSTATE / 10);
    if (broadcast_slave->state == EC_STATE_OPERATIONAL) {
      state_->cyclic_started = true;
      return absl::OkStatus();
    }
  }

  ecx_readstate(context);
  return absl::Status(absl::StatusCode::kUnavailable,
                      "SOEM EtherCAT slaves did not reach OPERATIONAL state");
}

absl::Status SoemEthercatTransport::StopCyclic() {
  if (!state_->initialized) {
    return absl::OkStatus();
  }
  if (!state_->cyclic_started) {
    return absl::OkStatus();
  }

  state_->context.slavelist[0].state = EC_STATE_INIT;
  ecx_writestate(&state_->context, 0);
  state_->cyclic_started = false;
  return absl::OkStatus();
}

absl::Status SoemEthercatTransport::Teardown() {
  if (!state_->initialized) {
    return absl::OkStatus();
  }

  ecx_close(&state_->context);
  std::memset(&state_->context, 0, sizeof(state_->context));
  state_->initialized = false;
  state_->configured = false;
  state_->cyclic_started = false;
  state_->io_map.clear();
  state_->mailbox_closed = false;
  state_->slaves.clear();
  state_->pdo_regions.clear();
  interface_name_.clear();
  process_data_mode_ = ProcessDataMode::kSplitLrdLwr;
  return absl::OkStatus();
}

absl::StatusOr<std::vector<SlaveIdentity>> SoemEthercatTransport::GetSlaves() const {
  if (!state_->configured) {
    return NotConfiguredStatus("GetSlaves");
  }
  return state_->slaves;
}

absl::StatusOr<PdoRegion> SoemEthercatTransport::GetPdoRegion(uint16_t slave_index) const {
  if (!state_->configured) {
    return NotConfiguredStatus("GetPdoRegion");
  }
  if (slave_index == 0 || slave_index > state_->pdo_regions.size()) {
    return absl::Status(absl::StatusCode::kNotFound, "SOEM EtherCAT slave index is not configured");
  }
  return state_->pdo_regions[static_cast<size_t>(slave_index - 1)];
}

absl::Status SoemEthercatTransport::WriteOutputs(const PdoRegion& region,
                                                 const std::vector<uint8_t>& outputs) {
  if (!state_->configured) {
    return NotConfiguredStatus("WriteOutputs");
  }
  if (outputs.size() != region.output_size_bytes) {
    return absl::Status(absl::StatusCode::kInvalidArgument,
                        "SOEM EtherCAT output size does not match PDO region");
  }

  constexpr uint8_t kDefaultGroup = 0;
  ec_groupt& group = state_->context.grouplist[kDefaultGroup];
  absl::Status bounds_status = ValidateRegionBounds(region, group);
  if (!bounds_status.ok()) {
    return bounds_status;
  }
  if (region.output_size_bytes == 0) {
    return absl::OkStatus();
  }

  std::copy(outputs.begin(), outputs.end(), group.outputs + region.output_offset_bytes);
  return absl::OkStatus();
}

absl::StatusOr<std::vector<uint8_t>> SoemEthercatTransport::ReadInputs(
    const PdoRegion& region) const {
  if (!state_->configured) {
    return NotConfiguredStatus("ReadInputs");
  }

  constexpr uint8_t kDefaultGroup = 0;
  const ec_groupt& group = state_->context.grouplist[kDefaultGroup];
  absl::Status bounds_status = ValidateRegionBounds(region, group);
  if (!bounds_status.ok()) {
    return bounds_status;
  }

  std::vector<uint8_t> inputs(region.input_size_bytes, 0);
  if (region.input_size_bytes > 0) {
    std::copy(group.inputs + region.input_offset_bytes,
              group.inputs + region.input_offset_bytes + region.input_size_bytes,
              inputs.begin());
  }
  return inputs;
}

absl::StatusOr<ProcessData> SoemEthercatTransport::ExchangeProcessData() {
  return ExchangeProcessData(EC_TIMEOUTRET);
}

absl::StatusOr<ProcessData> SoemEthercatTransport::ExchangeProcessData(int timeout_us) {
  if (timeout_us <= 0) return absl::InvalidArgumentError("process timeout must be positive");
  if (!state_->configured) {
    return NotConfiguredStatus("ExchangeProcessData");
  }
  if (!state_->cyclic_started) {
    return NotStartedStatus("ExchangeProcessData");
  }

  constexpr uint8_t kDefaultGroup = 0;
  ecx_contextt* context = &state_->context;
  ec_groupt& group = context->grouplist[kDefaultGroup];

  ecx_send_processdata_group(context, kDefaultGroup);
  const int working_count = ecx_receive_processdata_group(context, kDefaultGroup, timeout_us);

  ProcessData process_data;
  process_data.expected_working_count = ExpectedWorkingCount(group);
  process_data.working_count = working_count;
  if (group.Obytes > 0) {
    process_data.outputs.assign(group.outputs, group.outputs + group.Obytes);
  }
  if (group.Ibytes > 0) {
    process_data.inputs.assign(group.inputs, group.inputs + group.Ibytes);
  }
  return process_data;
}

absl::Status SoemEthercatTransport::CheckOperational(int timeout_us) {
  if (!state_->cyclic_started) return NotStartedStatus("CheckOperational");
  if (timeout_us <= 0) return absl::InvalidArgumentError("state timeout must be positive");
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::microseconds(timeout_us);
  auto* context = &state_->context;
  for (int i = 1; i <= context->slavecount; ++i) {
    const auto remaining = std::chrono::duration_cast<std::chrono::microseconds>(
                               deadline - std::chrono::steady_clock::now())
                               .count();
    if (remaining <= 0) return absl::DeadlineExceededError("AL state check exceeded budget");
    uint16_t state = 0;
    const int wkc = ecx_FPRD(&context->port,
                             context->slavelist[i].configadr,
                             ECT_REG_ALSTAT,
                             sizeof(state),
                             &state,
                             static_cast<int>(remaining));
    if (wkc != 1 || etohs(state) != EC_STATE_OPERATIONAL)
      return absl::UnavailableError("EtherCAT slave " + std::to_string(i) + " is not OPERATIONAL");
  }
  return absl::OkStatus();
}

absl::Status SoemEthercatTransport::ValidateSdo(SdoAddress address,
                                                size_t size,
                                                int timeout_us) const {
  if (!state_->configured) return NotConfiguredStatus("SDO access");
  if (state_->mailbox_closed)
    return absl::FailedPreconditionError("blocking SOEM SDO is forbidden during cyclic operation");
  if (address.slave == 0 || address.slave > state_->slaves.size() || size == 0 || size > 4096 ||
      timeout_us <= 0)
    return absl::InvalidArgumentError("invalid SDO slave, size (1..4096), or timeout");
  const auto& slave = state_->context.slavelist[address.slave];
  if (!(slave.mbx_proto & ECT_MBXPROT_COE) || slave.mbx_l < 16 || slave.mbx_rl < 16)
    return absl::FailedPreconditionError("slave has no usable CoE mailbox");
  return absl::OkStatus();
}

absl::StatusOr<std::vector<uint8_t>> SoemEthercatTransport::ReadSdo(SdoAddress address,
                                                                    size_t capacity,
                                                                    int timeout_us) {
  const auto valid = ValidateSdo(address, capacity, timeout_us);
  if (!valid.ok()) return valid;
  std::vector<uint8_t> bytes(capacity);
  int size = static_cast<int>(capacity);
  // Pinned SOEM 2.0.0 src/ec_coe.c uses EC_TIMEOUTTXM and repeated receive
  // waits internally. This argument does not bound total transfer duration.
  const int wkc = ecx_SDOread(&state_->context,
                              address.slave,
                              address.index,
                              address.subindex,
                              FALSE,
                              &size,
                              bytes.data(),
                              timeout_us);
  if (wkc <= 0) return absl::UnavailableError("SOEM SDO read failed (abort or transport failure)");
  if (size < 0 || static_cast<size_t>(size) > capacity)
    return absl::DataLossError("SOEM returned an invalid SDO size");
  bytes.resize(static_cast<size_t>(size));
  return bytes;
}

absl::Status SoemEthercatTransport::WriteSdo(SdoAddress address,
                                             const std::vector<uint8_t>& bytes,
                                             int timeout_us) {
  const auto valid = ValidateSdo(address, bytes.size(), timeout_us);
  if (!valid.ok()) return valid;
  const int wkc = ecx_SDOwrite(&state_->context,
                               address.slave,
                               address.index,
                               address.subindex,
                               FALSE,
                               static_cast<int>(bytes.size()),
                               bytes.data(),
                               timeout_us);
  if (wkc <= 0)
    return absl::UnavailableError(
        "SOEM SDO write failed (abort or transport failure); outcome unknown");
  return absl::OkStatus();
}

}  // namespace robot::comm::ethercat
