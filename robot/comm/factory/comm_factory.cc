#include "robot/comm/factory/comm_factory.h"

#include <boost/asio.hpp>
#include <map>
#include <mutex>
#include <thread>

#include "robot/comm/ethercat/ethercat_transport.h"
#include "robot/comm/ethercat/soem_ethercat_transport.h"
#include "robot/comm/serial/caller_sized_serial_transport.h"
#include "robot/comm/serial/joshua_wire_serial_transport.h"
#include "robot/comm/serial/serial.h"
#include "robot/comm/serial/serial_config_validation.h"

namespace robot::comm {

namespace {
constexpr uint32_t kDefaultSerialExchangeTimeoutMs = 100;

// One serial connection per port, with a fixed baud rate and shared I/O lock.
struct PortResources {
  std::shared_ptr<boost::asio::io_context> io_context{std::make_shared<boost::asio::io_context>()};
  boost::asio::executor_work_guard<boost::asio::io_context::executor_type> work_guard{
      boost::asio::make_work_guard(*io_context)};
  std::thread io_context_thread{[this] { io_context->run(); }};
  uint32_t baudrate = 0;
  std::shared_ptr<robot::comm::Serial> serial;
  ~PortResources() {
    work_guard.reset();
    if (io_context_thread.joinable()) io_context_thread.join();
  }
};

static std::mutex g_serial_mutex;
static std::map<std::string, std::unique_ptr<PortResources>> g_port_resources;  // keyed by port

// One SOEM master per interface, with a fixed process-data mode.
struct CachedEthercatTransport {
  robot::comm::ethercat::ProcessDataMode process_data_mode;
  std::shared_ptr<robot::comm::ethercat::EthercatTransport> transport;
};

static std::mutex g_ethercat_mutex;
static std::map<std::string, CachedEthercatTransport>
    g_ethercat_transports;  // keyed by interface name
static std::function<std::shared_ptr<robot::comm::ethercat::EthercatTransport>()>
    g_ethercat_transport_factory_for_testing;
static std::function<absl::StatusOr<CommTransport>(const robot::comm::Comm&)>
    g_comm_transport_factory_for_testing;

absl::StatusOr<robot::comm::ethercat::ProcessDataMode> ToTransportProcessDataMode(
    EthercatProcessDataMode process_data_mode) {
  switch (process_data_mode) {
    case EthercatProcessDataMode::ETHERCAT_PROCESS_DATA_MODE_SPLIT_LRD_LWR:
      return robot::comm::ethercat::ProcessDataMode::kSplitLrdLwr;
    case EthercatProcessDataMode::ETHERCAT_PROCESS_DATA_MODE_LRW:
      return robot::comm::ethercat::ProcessDataMode::kLrw;
    case EthercatProcessDataMode::ETHERCAT_PROCESS_DATA_MODE_INVALID:
    default:
      return absl::Status(absl::StatusCode::kInvalidArgument,
                          "EtherCAT config has invalid process data mode");
  }
}
absl::StatusOr<std::shared_ptr<Serial>> CreateSerial(const robot::comm::SerialConfig& config) {
  if (config.port().empty()) {
    return absl::Status(absl::StatusCode::kInvalidArgument, "Serial config has no port");
  }

  if (config.baudrate() == 0) {
    return absl::Status(absl::StatusCode::kInvalidArgument, "Serial config has no baudrate");
  }

  const std::string& port = config.port();
  const uint32_t baudrate = config.baudrate();

  std::lock_guard<std::mutex> lock(g_serial_mutex);
  auto& port_res_ptr = g_port_resources[port];
  if (!port_res_ptr) {
    port_res_ptr = std::make_unique<PortResources>();
  }

  if (port_res_ptr->serial) {
    if (port_res_ptr->baudrate != baudrate) {
      return absl::InvalidArgumentError("Serial port " + port + " is already open at baud rate " +
                                        std::to_string(port_res_ptr->baudrate) + "; requested " +
                                        std::to_string(baudrate));
    }
    return port_res_ptr->serial;
  }

  auto serial = std::make_shared<Serial>(port_res_ptr->io_context,
                                         port,
                                         baudrate,
                                         std::chrono::milliseconds(config.post_open_settle_ms()));
  port_res_ptr->baudrate = baudrate;
  port_res_ptr->serial = serial;
  return serial;
}

}  // namespace

const std::vector<CommImplementation>& CommFactory::Implementations() {
  static const std::vector<CommImplementation> entries = {
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
                     std::chrono::milliseconds(comm.serial_config().has_exchange_timeout_ms()
                                                   ? comm.serial_config().exchange_timeout_ms()
                                                   : kDefaultSerialExchangeTimeoutMs)))};
           default:
             return absl::InvalidArgumentError("Comm has an invalid serial message_adapter.");
         }
       },
       "serial_config requires port/baudrate and explicit message_adapter CALLER_SIZED or "
       "JOSHUA_WIRE; exchange_timeout_ms is JOSHUA_WIRE-only."},
      {ETHERCAT,
       CYCLIC,
       "installed",
       [](const Comm& comm) -> absl::StatusOr<CommTransport> {
         if (!comm.has_ethercat_config()) {
           return absl::InvalidArgumentError("ETHERCAT comm has no ethercat_config.");
         }
         auto transport = CreateEthercat(comm.ethercat_config());
         if (!transport.ok()) return transport.status();
         return CommTransport{*transport};
       },
       "ethercat_config requires interface_name and an explicit supported process_data_mode; "
       "retained TI demo transport."},
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
    return absl::InvalidArgumentError("SERIAL requires BYTE_STREAM or MESSAGE transport_type.");
  }
  if (comm.comm_type() == ETHERCAT) {
    return absl::InvalidArgumentError("ETHERCAT requires CYCLIC transport_type.");
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

void CommFactory::ResetSerialTransportCacheForTesting() {
  std::lock_guard<std::mutex> lock(g_serial_mutex);
  g_port_resources.clear();
}

absl::StatusOr<std::shared_ptr<robot::comm::ethercat::EthercatTransport>>
CommFactory::CreateEthercat(const robot::comm::EthercatConfig& config) {
  if (config.interface_name().empty()) {
    return absl::Status(absl::StatusCode::kInvalidArgument,
                        "EtherCAT config has no interface name");
  }

  auto process_data_mode_or = ToTransportProcessDataMode(config.process_data_mode());
  if (!process_data_mode_or.ok()) {
    return process_data_mode_or.status();
  }

  const std::string& interface_name = config.interface_name();

  std::lock_guard<std::mutex> lock(g_ethercat_mutex);
  auto it = g_ethercat_transports.find(interface_name);
  if (it != g_ethercat_transports.end()) {
    if (it->second.process_data_mode != *process_data_mode_or) {
      return absl::Status(absl::StatusCode::kInvalidArgument,
                          "EtherCAT interface " + interface_name +
                              " is already open with a different process data mode");
    }
    return it->second.transport;
  }

  std::shared_ptr<robot::comm::ethercat::EthercatTransport> transport;
  if (g_ethercat_transport_factory_for_testing) {
    transport = g_ethercat_transport_factory_for_testing();
  } else {
    transport = std::make_shared<robot::comm::ethercat::SoemEthercatTransport>();
  }
  auto status = transport->Init(interface_name, *process_data_mode_or);
  if (!status.ok()) {
    return status;
  }
  g_ethercat_transports[interface_name] = CachedEthercatTransport{*process_data_mode_or, transport};
  return transport;
}

void CommFactory::SetEthercatTransportFactoryForTesting(
    std::function<std::shared_ptr<robot::comm::ethercat::EthercatTransport>()> factory) {
  std::lock_guard<std::mutex> lock(g_ethercat_mutex);
  g_ethercat_transport_factory_for_testing = std::move(factory);
}

void CommFactory::ResetEthercatTransportCacheForTesting() {
  std::lock_guard<std::mutex> lock(g_ethercat_mutex);
  for (auto& [interface_name, cached] : g_ethercat_transports) {
    cached.transport->Teardown().IgnoreError();
  }
  g_ethercat_transports.clear();
}
}  // namespace robot::comm
