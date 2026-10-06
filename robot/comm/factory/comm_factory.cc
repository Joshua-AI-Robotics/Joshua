#include "robot/comm/factory/comm_factory.h"

#include <algorithm>
#include <boost/asio.hpp>
#include <climits>
#include <map>
#include <mutex>
#include <thread>

#include "firmware/common/joshua_wire_ethercat.h"
#include "robot/comm/ethercat/ethercat_types.h"
#include "robot/comm/ethercat/joshua_wire_ethercat_transport.h"
#include "robot/comm/ethercat/soem_ethercat_backend.h"
#include "robot/comm/serial/framed_serial_transport.h"
#include "robot/comm/serial/serial.h"
#include "utils/status_macros.h"

namespace robot::comm {

namespace {
// One physical open per port. Consumers must agree on link and timing policy.
struct PortResources {
  std::shared_ptr<boost::asio::io_context> io_context{std::make_shared<boost::asio::io_context>()};
  boost::asio::executor_work_guard<boost::asio::io_context::executor_type> work_guard{
      boost::asio::make_work_guard(*io_context)};
  std::thread io_context_thread{[this] { io_context->run(); }};
  std::shared_ptr<Serial> serial;
  SerialConfig config;
  ~PortResources() {
    work_guard.reset();
    if (io_context_thread.joinable()) io_context_thread.join();
  }
};

static std::mutex g_serial_mutex;
static std::map<std::string, std::unique_ptr<PortResources>> g_port_resources;  // keyed by port

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

  const std::string& port = config.port();
  const auto timeout = [](const SerialConfig& c) {
    return c.has_exchange_timeout_ms() ? c.exchange_timeout_ms() : 100u;
  };

  std::lock_guard<std::mutex> lock(g_serial_mutex);
  auto& port_res_ptr = g_port_resources[port];
  if (!port_res_ptr) {
    port_res_ptr = std::make_unique<PortResources>();
  }

  if (port_res_ptr->serial) {
    const auto& existing = port_res_ptr->config;
    if (existing.baudrate() != config.baudrate() ||
        existing.post_open_settle_ms() != config.post_open_settle_ms() ||
        timeout(existing) != timeout(config))
      return absl::InvalidArgumentError("Serial port already has different baudrate/timing policy");
    return port_res_ptr->serial;
  }
  try {
    port_res_ptr->serial =
        std::make_shared<Serial>(port_res_ptr->io_context,
                                 port,
                                 static_cast<int>(config.baudrate()),
                                 std::chrono::milliseconds(config.post_open_settle_ms()));
  } catch (const std::exception& e) {
    return absl::UnavailableError(e.what());
  }
  port_res_ptr->config = config;
  return port_res_ptr->serial;
}

}  // namespace

absl::StatusOr<CommTransport> CommFactory::CreateComm(const robot::comm::Comm& comm) {
  if (g_comm_transport_factory_for_testing) {
    return g_comm_transport_factory_for_testing(comm);
  }
  switch (comm.comm_type()) {
    case CommType::SERIAL: {
      if (!comm.has_serial_config()) {
        return absl::InvalidArgumentError("SERIAL comm has no serial_config.");
      }
      if (comm.transport_type() != TransportType::BYTE_STREAM &&
          comm.transport_type() != TransportType::MESSAGE) {
        return absl::InvalidArgumentError("SERIAL requires BYTE_STREAM or MESSAGE transport_type.");
      }
      auto serial_or = CreateSerial(comm.serial_config());
      if (!serial_or.ok()) {
        return serial_or.status();
      }
      switch (comm.transport_type()) {
        case TransportType::BYTE_STREAM:
          return CommTransport{std::static_pointer_cast<ByteStream>(*serial_or)};
        case TransportType::MESSAGE: {
          const auto& config = comm.serial_config();
          return CommTransport{
              std::static_pointer_cast<MessageTransport>(std::make_shared<FramedSerialTransport>(
                  *serial_or,
                  std::chrono::milliseconds(
                      config.has_exchange_timeout_ms() ? config.exchange_timeout_ms() : 100)))};
        }
        case TransportType::CYCLIC:
        case TransportType::TRANSPORT_INVALID:
        default:
          return absl::InvalidArgumentError("Comm has an invalid transport_type.");
      }
    }
    case CommType::ETHERCAT: {
      if (comm.transport_type() == TransportType::MESSAGE_AND_CYCLIC) {
        if (!comm.has_ethercat_config())
          return absl::InvalidArgumentError("ETHERCAT comm has no ethercat_config.");
        return CreatePairedEthercat(comm.ethercat_config());
      }
      return absl::InvalidArgumentError(
          "Legacy TI-demo EtherCAT is retired; select MESSAGE_AND_CYCLIC with explicit JW "
          "board protocol, matching firmware and comm.ethercat_config endpoint/timing fields.");
    }
    case CommType::ETHERNET_UDP:
      if (comm.transport_type() != TransportType::MESSAGE) {
        return absl::InvalidArgumentError("ETHERNET_UDP requires MESSAGE transport_type.");
      }
      return absl::UnimplementedError("ETHERNET_UDP communication is not implemented.");
    case CommType::COMM_INVALID:
    default:
      return absl::InvalidArgumentError("Comm has an invalid comm_type.");
  }
}

void CommFactory::SetCommTransportFactoryForTesting(
    std::function<absl::StatusOr<CommTransport>(const robot::comm::Comm&)> factory) {
  g_comm_transport_factory_for_testing = std::move(factory);
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
  if (config.port().empty() || config.baudrate() == 0 || config.baudrate() > INT_MAX)
    return absl::InvalidArgumentError("Serial requires a port and baudrate in 1..INT_MAX");
  if ((config.has_exchange_timeout_ms() &&
       (config.exchange_timeout_ms() == 0 || config.exchange_timeout_ms() > INT_MAX)) ||
      config.post_open_settle_ms() > INT_MAX)
    return absl::InvalidArgumentError(
        "Serial exchange_timeout_ms must be 1..INT_MAX; post_open_settle_ms must be 0..INT_MAX");
  return absl::OkStatus();
}

absl::Status CommFactory::ValidatePairedEthercatConfig(const EthercatConfig& config) {
  if (config.interface_name().empty() || config.slave_index() == 0 ||
      config.slave_index() > UINT16_MAX ||
      config.process_data_mode() != ETHERCAT_PROCESS_DATA_MODE_SPLIT_LRD_LWR)
    return absl::InvalidArgumentError(
        "Paired EtherCAT requires NIC, slave_index 1..65535 and split LRD/LWR");
  const auto& t = config.timing();
  for (uint32_t value : {t.period_us(),
                         t.process_timeout_us(),
                         t.state_timeout_us(),
                         t.operation_timeout_us(),
                         t.mailbox_step_budget_us(),
                         t.scheduling_guard_us(),
                         t.response_timeout_us()}) {
    if (value == 0 || value > INT_MAX)
      return absl::InvalidArgumentError(
          "All EtherCAT timing budgets must be explicit, 1..INT_MAX microseconds");
  }
  if (uint64_t(t.period_us()) <= uint64_t(t.process_timeout_us()) +
                                     std::max(t.state_timeout_us(), t.mailbox_step_budget_us()) +
                                     t.scheduling_guard_us() ||
      t.response_timeout_us() <= t.period_us() || t.operation_timeout_us() <= t.period_us())
    return absl::InvalidArgumentError(
        "EtherCAT timing budgets do not fit the cycle/response deadlines");
  if (config.has_pdo_region() && (config.pdo_region().output_size_bytes() != JWEC_PDO_SIZE ||
                                  config.pdo_region().input_size_bytes() != JWEC_PDO_SIZE))
    return absl::InvalidArgumentError("JW layout-v1 requires 80-byte PDO region assertions");
  return absl::OkStatus();
}
}  // namespace robot::comm
