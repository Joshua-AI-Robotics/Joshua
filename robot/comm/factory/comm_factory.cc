#include "robot/comm/factory/comm_factory.h"

#include <map>
#include <mutex>
#include <optional>

#include "robot/comm/ethercat/ethercat_types.h"
#include "robot/comm/ethercat/joshua_wire_ethercat_transport.h"
#include "robot/comm/ethercat/soem_ethercat_backend.h"
#include "robot/comm/factory/comm_config_validation.h"
#include "robot/comm/linux_bus/linux_bus.h"
#include "robot/comm/serial/caller_sized_serial_transport.h"
#include "robot/comm/serial/joshua_wire_frame_stream.h"
#include "robot/comm/serial/joshua_wire_serial_transport.h"
#include "robot/comm/serial/serial.h"
#include "robot/comm/serial/serial_config_validation.h"
#include "utils/joshua_runtime.h"
#include "utils/status_macros.h"

namespace robot::comm {

namespace {
// Weak leases release the physical owner when its last consumer goes away.
// The owner's process claim remains reserved throughout close/destruction.
struct PortResources {
  std::weak_ptr<Serial> serial;
  std::weak_ptr<JoshuaWireFrameStream> frames;
  SerialConfig config;
  std::optional<uint32_t> joshua_timeout_ms;
};
static auto& g_serial_mutex = *new std::mutex;
static auto& g_port_resources = *new std::map<std::string, PortResources>;

// Process-lifetime synchronization/cache storage avoids cross-translation-unit
// static destruction races with BoardFactory's retained board instances.
static std::mutex& g_ethercat_mutex = *new std::mutex;
static std::function<absl::StatusOr<CommTransport>(const robot::comm::Comm&)>
    g_comm_transport_factory_for_testing;

// Leases retire the cache entry under the NIC mutex after stopping the owner.
// A weak cache alone would permit reopening while the old destructor is still
// closing its socket. Unused slaves receive the validated stop image.
struct PairedBus {
  std::shared_ptr<ethercat::EthercatMaster> master;
  std::vector<std::shared_ptr<ethercat::JoshuaWireEthercatTransport>> endpoints;
  std::vector<bool> claimed;
  std::string timing_key;
  std::string interface_name;
  size_t leases = 0;
  void Stop() {
    for (auto& endpoint : endpoints) endpoint->Stop();
    if (master) master->Stop().IgnoreError();
  }
  ~PairedBus() {
    Stop();
  }
};
static auto& g_paired_buses = *new std::map<std::string, std::shared_ptr<PairedBus>>;
struct EndpointLease {
  std::shared_ptr<PairedBus> bus;
  size_t index;
  ~EndpointLease() {
    std::lock_guard lock(g_ethercat_mutex);
    bus->endpoints[index]->Stop();
    if (--bus->leases == 0) {
      bus->Stop();
      g_paired_buses.erase(bus->interface_name);
    }
  }
};
static std::function<std::unique_ptr<ethercat::EthercatMasterIo>()> g_master_io_factory;

absl::Status CheckRegion(const EthercatConfig& config, const ethercat::PdoRegion& region) {
  if (!config.has_pdo_region()) return absl::OkStatus();
  const auto& expected = config.pdo_region();
  if (expected.output_offset_bytes() != region.output_offset_bytes ||
      expected.input_offset_bytes() != region.input_offset_bytes ||
      expected.output_size_bytes() != region.output_size_bytes ||
      expected.input_size_bytes() != region.input_size_bytes)
    return absl::FailedPreconditionError("EtherCAT PDO region assertion differs from discovery");
  return absl::OkStatus();
}

absl::StatusOr<CommTransport> CreatePairedEthercat(const EthercatConfig& config) {
  ABSL_RETURN_IF_ERROR(CommFactory::ValidatePairedEthercatConfig(config));
  std::lock_guard lock(g_ethercat_mutex);
  const auto found = g_paired_buses.find(config.interface_name());
  auto bus = found == g_paired_buses.end() ? nullptr : found->second;
  const auto& t = config.timing();
  const std::string key = t.SerializeAsString();
  if (bus && bus->timing_key != key)
    return absl::InvalidArgumentError("EtherCAT NIC already has different timing policy");
  const size_t index = config.slave_index() - 1;
  if (!bus) {
    using Us = ethercat::EthercatMaster::Microseconds;
    auto io = g_master_io_factory ? g_master_io_factory()
                                  : std::make_unique<ethercat::SoemEthercatBackend>();
    ABSL_ASSIGN_OR_RETURN(auto opened,
                          ethercat::EthercatMaster::Open(std::move(io),
                                                         config.interface_name(),
                                                         ethercat::ProcessDataMode::kSplitLrdLwr,
                                                         {Us(t.period_us()),
                                                          Us(t.process_timeout_us()),
                                                          Us(t.state_timeout_us()),
                                                          Us(t.operation_timeout_us()),
                                                          Us(t.mailbox_step_budget_us()),
                                                          Us(t.scheduling_guard_us())}));
    bus = std::make_shared<PairedBus>();
    bus->master = std::move(opened);
    bus->timing_key = key;
    bus->interface_name = config.interface_name();
    if (index >= bus->master->regions().size())
      return absl::InvalidArgumentError("EtherCAT slave_index exceeds discovered slave count");
    ABSL_RETURN_IF_ERROR(CheckRegion(config, bus->master->regions()[index]));
    std::vector<ethercat::EthercatMaster::Bytes> stops;
    for (const auto& region : bus->master->regions()) {
      ABSL_ASSIGN_OR_RETURN(
          auto endpoint,
          ethercat::JoshuaWireEthercatTransport::Open(
              bus->master, region.slave_index, {Us(t.response_timeout_us()), Us(t.period_us())}));
      bus->endpoints.push_back(std::move(endpoint));
      stops.push_back(ethercat::JoshuaWireEthercatTransport::StopImage());
    }
    bus->claimed.resize(bus->endpoints.size(), false);
    ABSL_RETURN_IF_ERROR(bus->master->StartCyclic(std::move(stops)));
  }
  ABSL_RETURN_IF_ERROR(bus->master->status());
  if (index >= bus->endpoints.size())
    return absl::InvalidArgumentError("EtherCAT slave_index exceeds discovered slave count");
  ABSL_RETURN_IF_ERROR(CheckRegion(config, bus->master->regions()[index]));
  if (bus->claimed[index])
    return absl::AlreadyExistsError(
        "EtherCAT endpoint already leased; release the bus before reopening");
  bus->claimed[index] = true;
  auto lease = std::make_shared<EndpointLease>();
  lease->bus = bus;
  lease->index = index;
  ++bus->leases;
  // Publish only after all fallible initialization checks and the first lease.
  // A fault between StartCyclic and status() must not leave a zero-lease cache.
  g_paired_buses[config.interface_name()] = bus;
  auto* endpoint = bus->endpoints[index].get();
  return CommTransport{PairedTransports{std::shared_ptr<MessageTransport>(lease, endpoint),
                                        std::shared_ptr<CorrelatedCyclicTransport>(lease, endpoint),
                                        absl::Microseconds(t.response_timeout_us())}};
}

absl::StatusOr<std::shared_ptr<Serial>> CreateSerial(const robot::comm::SerialConfig& config) {
  ABSL_RETURN_IF_ERROR(CommFactory::ValidateSerialConfig(config));

  ABSL_ASSIGN_OR_RETURN(auto identity, SerialIoOwner::Identify(config.port()));
  const auto timeout = [](const SerialConfig& c) {
    return c.has_exchange_timeout_ms() ? c.exchange_timeout_ms() : kDefaultSerialExchangeTimeoutMs;
  };
  std::lock_guard lock(g_serial_mutex);
  // Prune retired metadata rather than retaining every port path forever.
  for (auto it = g_port_resources.begin(); it != g_port_resources.end();) {
    if (it->second.serial.expired() && it->second.frames.expired())
      it = g_port_resources.erase(it);
    else
      ++it;
  }
  auto found = g_port_resources.find(identity.key);
  if (found != g_port_resources.end()) {
    auto& resources = found->second;
    if (!resources.frames.expired())
      return absl::InvalidArgumentError("FRAME_STREAM cannot share a port with request-only users");
    if (auto serial = resources.serial.lock()) {
      const auto& existing = resources.config;
      if (existing.baudrate() != config.baudrate())
        return absl::InvalidArgumentError("Serial port " + config.port() +
                                          " is already open at baud rate " +
                                          std::to_string(existing.baudrate()) + "; requested " +
                                          std::to_string(config.baudrate()));
      if (existing.post_open_settle_ms() != config.post_open_settle_ms() ||
          SerialOptions(existing) != SerialOptions(config))
        return absl::InvalidArgumentError("Serial port already has different timing/I/O policy");
      ABSL_RETURN_IF_ERROR(serial->Open());
      if (config.message_adapter() == JOSHUA_WIRE) {
        if (resources.joshua_timeout_ms && *resources.joshua_timeout_ms != timeout(config))
          return absl::InvalidArgumentError(
              "Serial port already has different JoshuaWire timing policy");
        resources.joshua_timeout_ms = timeout(config);
      }
      return serial;
    }
  }
  ABSL_ASSIGN_OR_RETURN(auto serial,
                        Serial::Create(config.port(),
                                       static_cast<int>(config.baudrate()),
                                       std::chrono::milliseconds(config.post_open_settle_ms()),
                                       SerialOptions(config)));
  if (serial->identity().key != identity.key)
    return absl::UnavailableError("Serial device changed during factory open");
  PortResources resources;
  resources.serial = serial;
  resources.config = config;
  if (config.message_adapter() == JOSHUA_WIRE) resources.joshua_timeout_ms = timeout(config);
  g_port_resources[identity.key] = std::move(resources);
  return serial;
}

absl::StatusOr<CommTransport> CreateFrameStream(const Comm& comm) {
  ABSL_RETURN_IF_ERROR(ValidateSerialCommConfig(comm));
  const auto& config = comm.serial_config();
  ABSL_RETURN_IF_ERROR(CommFactory::ValidateSerialConfig(config));
  ABSL_ASSIGN_OR_RETURN(auto identity, SerialIoOwner::Identify(config.port()));
  std::lock_guard lock(g_serial_mutex);
  for (auto it = g_port_resources.begin(); it != g_port_resources.end();) {
    if (it->second.serial.expired() && it->second.frames.expired())
      it = g_port_resources.erase(it);
    else
      ++it;
  }
  auto found = g_port_resources.find(identity.key);
  if (found != g_port_resources.end()) {
    auto& resources = found->second;
    if (!resources.serial.expired())
      return absl::InvalidArgumentError("Request-only port cannot acquire a continuous receiver");
    if (auto frames = resources.frames.lock()) {
      const auto& existing = resources.config;
      if (existing.baudrate() != config.baudrate() ||
          existing.post_open_settle_ms() != config.post_open_settle_ms() ||
          SerialOptions(existing) != SerialOptions(config) ||
          SerialFramePolicyOptions(existing) != SerialFramePolicyOptions(config))
        return absl::InvalidArgumentError("Frame stream port already has different I/O policy");
      ABSL_RETURN_IF_ERROR(frames->status());
      return CommTransport{std::static_pointer_cast<FrameStreamTransport>(frames)};
    }
  }
  ABSL_ASSIGN_OR_RETURN(
      auto frames,
      JoshuaWireFrameStream::Open(config.port(),
                                  static_cast<int>(config.baudrate()),
                                  std::chrono::milliseconds(config.post_open_settle_ms()),
                                  SerialOptions(config),
                                  SerialFramePolicyOptions(config)));
  if (frames->identity().key != identity.key)
    return absl::UnavailableError("Serial device changed during frame stream open");
  PortResources resources;
  resources.frames = frames;
  resources.config = config;
  g_port_resources[identity.key] = std::move(resources);
  return CommTransport{std::static_pointer_cast<FrameStreamTransport>(frames)};
}

}  // namespace

const std::vector<CommImplementation>& CommFactory::Implementations() {
  static const std::vector<CommImplementation> entries = {
      {SPI,
       SPI_TRANSFER,
       "installed",
       [](const Comm& c) -> absl::StatusOr<CommTransport> {
         if (!c.has_spi_config()) return absl::InvalidArgumentError("SPI requires spi_config");
         ABSL_ASSIGN_OR_RETURN(auto bus, linux_bus::OpenSpi(c.spi_config()));
         return CommTransport{std::move(bus)};
       },
       "8-bit SPI, mode 0..3, transfers 1..32 bytes, explicit controller identity"},
      {I2C,
       I2C_TRANSFER,
       "installed",
       [](const Comm& c) -> absl::StatusOr<CommTransport> {
         if (!c.has_i2c_config()) return absl::InvalidArgumentError("I2C requires i2c_config");
         ABSL_ASSIGN_OR_RETURN(auto bus, linux_bus::OpenI2c(c.i2c_config()));
         return CommTransport{std::move(bus)};
       },
       "7-bit addressed I2C, combined transfers <=32 bytes, explicit controller identity"},
      {SERIAL,
       FRAME_STREAM,
       "installed",
       &CreateFrameStream,
       "Host integration capability; existing board consumers remain request-only. "
       "JOSHUA_WIRE continuous receive only; one coordinator, exclusive from raw/request-only "
       "users."},
      {SERIAL,
       BYTE_STREAM,
       "installed",
       [](const Comm& comm) -> absl::StatusOr<CommTransport> {
         auto status = ValidateSerialCommConfig(comm);
         if (!status.ok()) return status;
         auto serial = CreateSerial(comm.serial_config());
         if (!serial.ok()) return serial.status();
         return CommTransport{std::static_pointer_cast<ByteStream>(*serial)};
       },
       "serial_config requires port/baudrate; message_adapter and exchange_timeout_ms must be "
       "omitted."},
      {SERIAL,
       MESSAGE,
       "installed",
       [](const Comm& comm) -> absl::StatusOr<CommTransport> {
         auto status = ValidateSerialCommConfig(comm);
         if (!status.ok()) return status;
         auto serial = CreateSerial(comm.serial_config());
         if (!serial.ok()) return serial.status();
         switch (comm.serial_config().message_adapter()) {
           case CALLER_SIZED:
             return CommTransport{std::static_pointer_cast<MessageTransport>(
                 std::make_shared<CallerSizedSerialTransport>(*serial))};
           case JOSHUA_WIRE:
             return CommTransport{std::static_pointer_cast<MessageTransport>(
                 std::make_shared<JoshuaWireSerialTransport>(
                     *serial,
                     static_cast<double>(comm.serial_config().has_exchange_timeout_ms()
                                             ? comm.serial_config().exchange_timeout_ms()
                                             : kDefaultSerialExchangeTimeoutMs) /
                         1000.0))};
           default:
             return absl::InvalidArgumentError("Comm has an invalid serial message_adapter.");
         }
       },
       "serial_config requires port/baudrate and explicit message_adapter CALLER_SIZED or "
       "JOSHUA_WIRE; exchange_timeout_ms is JOSHUA_WIRE-only."},
      {ETHERCAT,
       MESSAGE_AND_CYCLIC,
       "installed",
       [](const Comm& comm) -> absl::StatusOr<CommTransport> {
         if (!comm.has_ethercat_config()) {
           return absl::InvalidArgumentError("ETHERCAT comm has no ethercat_config.");
         }
         return CreatePairedEthercat(comm.ethercat_config());
       },
       "Explicit JOSHUA_WIRE board protocol and matching firmware are required; "
       "ethercat_config requires interface_name, slave_index, SPLIT_LRD_LWR process_data_mode "
       "and explicit timing fields. Legacy TI-demo CYCLIC transport is retired."},
      {ETHERNET_UDP, MESSAGE, "unimplemented", nullptr, "No implementation is available."}};
  return entries;
}

absl::StatusOr<CommTransport> CommFactory::CreateComm(const robot::comm::Comm& comm) {
  if (g_comm_transport_factory_for_testing) {
    return g_comm_transport_factory_for_testing(comm);
  }
  for (const auto& implementation : Implementations()) {
    if (implementation.mechanism != comm.comm_type() ||
        implementation.transport != comm.transport_type())
      continue;
    if (!implementation.create) {
      return absl::UnimplementedError("ETHERNET_UDP communication is not implemented.");
    }
    return implementation.create(comm);
  }
  if (comm.comm_type() == SERIAL) {
    return absl::InvalidArgumentError(
        "SERIAL requires BYTE_STREAM, MESSAGE, or FRAME_STREAM transport_type.");
  }
  if (comm.comm_type() == ETHERCAT) {
    return absl::InvalidArgumentError(
        "Legacy TI-demo EtherCAT is retired; select MESSAGE_AND_CYCLIC with explicit JW "
        "board protocol, matching firmware and comm.ethercat_config endpoint/timing fields.");
  }
  if (comm.comm_type() == ETHERNET_UDP) {
    return absl::InvalidArgumentError("ETHERNET_UDP requires MESSAGE transport_type.");
  }
  return absl::InvalidArgumentError("Comm has an invalid comm_type.");
}

void CommFactory::SetCommTransportFactoryForTesting(
    std::function<absl::StatusOr<CommTransport>(const robot::comm::Comm&)> factory) {
  g_comm_transport_factory_for_testing = std::move(factory);
}

std::vector<SerialDiagnostics> CommFactory::SnapshotSerialConnections() {
  std::vector<SerialDiagnostics> result;
  std::lock_guard lock(g_serial_mutex);
  for (auto it = g_port_resources.begin(); it != g_port_resources.end();) {
    if (auto serial = it->second.serial.lock()) {
      result.push_back(serial->Snapshot());
      ++it;
    } else if (auto frames = it->second.frames.lock()) {
      result.push_back(frames->Snapshot());
      ++it;
    } else {
      it = g_port_resources.erase(it);
    }
  }
  return result;
}

void CommFactory::ResetSerialTransportCacheForTesting() {
  std::lock_guard<std::mutex> lock(g_serial_mutex);
  g_port_resources.clear();
}

void CommFactory::ResetEthercatTransportCacheForTesting() {
  std::lock_guard<std::mutex> lock(g_ethercat_mutex);
  for (auto& [name, bus] : g_paired_buses) bus->Stop();
  // Keep live leases registered: even this test helper must not create a
  // second NIC owner while old capability handles still exist.
}

void CommFactory::SetEthercatMasterIoFactoryForTesting(
    std::function<std::unique_ptr<ethercat::EthercatMasterIo>()> factory) {
  std::lock_guard lock(g_ethercat_mutex);
  g_master_io_factory = std::move(factory);
}

absl::Status CommFactory::ValidateSerialConfig(const SerialConfig& config) {
  return robot::comm::ValidateSerialConfig(config);
}

absl::Status CommFactory::ValidatePairedEthercatConfig(const EthercatConfig& config) {
  return robot::comm::ValidatePairedEthercatConfig(config);
}

void CommFactory::ShutdownAll() noexcept {
  linux_bus::ShutdownAll();
  joshua::RunCleanup("serial transport shutdown", [] {
    std::vector<std::shared_ptr<Serial>> serials;
    std::vector<std::shared_ptr<JoshuaWireFrameStream>> streams;
    {
      std::lock_guard lock(g_serial_mutex);
      for (auto& [name, resources] : g_port_resources) {
        if (auto serial = resources.serial.lock()) serials.push_back(std::move(serial));
        if (auto stream = resources.frames.lock()) streams.push_back(std::move(stream));
      }
    }
    for (const auto& serial : serials)
      joshua::RunCleanup("serial owner close", [&] { joshua::ThrowIfError(serial->Close()); });
    for (const auto& stream : streams)
      joshua::RunCleanup("serial frame stream close",
                         [&] { joshua::ThrowIfError(stream->Close()); });
  });
  joshua::RunCleanup("EtherCAT transport shutdown", [] {
    std::lock_guard<std::mutex> lock(g_ethercat_mutex);
    for (auto& [name, bus] : g_paired_buses) {
      joshua::RunCleanup(name.c_str(), [&] { bus->Stop(); });
    }
  });
}
}  // namespace robot::comm
