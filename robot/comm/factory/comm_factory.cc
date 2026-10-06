#include "robot/comm/factory/comm_factory.h"

#include <boost/asio.hpp>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

#include "absl/strings/str_cat.h"
#include "robot/comm/ethercat/ethercat_transport.h"
#include "robot/comm/ethercat/soem_ethercat_transport.h"
#include "robot/comm/factory/transport_requirements.h"
#include "robot/comm/serial/serial.h"
#include "robot/comm/serial/serial_message_transport.h"
#include "utils/status_macros.h"

namespace robot::comm {

namespace {
// Shared serial resources keyed by port and baud rate.
struct PortResources {
  std::shared_ptr<boost::asio::io_context> io_context{std::make_shared<boost::asio::io_context>()};
  boost::asio::executor_work_guard<boost::asio::io_context::executor_type> work_guard{
      boost::asio::make_work_guard(*io_context)};
  std::thread io_context_thread{[this] { io_context->run(); }};
  std::map<uint32_t, std::shared_ptr<robot::comm::Serial>> serials;
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
static std::function<absl::StatusOr<CommLease>(const robot::comm::Comm&, const CommOptions&)>
    g_comm_lease_factory_for_testing;

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
  uint32_t baudrate = config.baudrate();

  std::lock_guard<std::mutex> lock(g_serial_mutex);
  auto& port_res_ptr = g_port_resources[port];
  if (!port_res_ptr) {
    port_res_ptr = std::make_unique<PortResources>();
  }

  auto& serials = port_res_ptr->serials;
  auto it = serials.find(baudrate);
  if (it != serials.end()) {
    return it->second;
  }

  auto serial = std::make_shared<Serial>(port_res_ptr->io_context, port, baudrate);
  serials[baudrate] = serial;
  return serial;
}

// Rejects configs whose mechanism cannot provide a required transport, before
// any link is opened.
absl::Status ValidateMechanism(const robot::comm::Comm& comm,
                               const TransportSet& required,
                               const CommOptions& options) {
  switch (comm.comm_type()) {
    case CommType::SERIAL:
      if (!comm.has_serial_config()) {
        return absl::InvalidArgumentError("SERIAL comm has no serial_config.");
      }
      for (const auto transport : required) {
        switch (transport) {
          case TransportType::BYTE_STREAM:
            break;
          case TransportType::MESSAGE:
            if (options.message_framer == nullptr) {
              return absl::InvalidArgumentError(
                  "SERIAL MESSAGE transport requires a message framer from its consumer.");
            }
            break;
          case TransportType::CYCLIC:
          case TransportType::CORRELATED_CYCLIC:
            return absl::InvalidArgumentError("SERIAL does not provide a cyclic transport.");
          default:
            return absl::InvalidArgumentError("Comm has an invalid transport_type.");
        }
      }
      return absl::OkStatus();
    case CommType::ETHERCAT:
      if (!comm.has_ethercat_config()) {
        return absl::InvalidArgumentError("ETHERCAT comm has no ethercat_config.");
      }
      for (const auto transport : required) {
        switch (transport) {
          case TransportType::CYCLIC:
            break;
          case TransportType::MESSAGE:
          case TransportType::CORRELATED_CYCLIC:
            return absl::UnimplementedError(absl::StrCat(
                "ETHERCAT ", TransportType_Name(transport), " transport is not implemented yet."));
          default:
            return absl::InvalidArgumentError(absl::StrCat(
                "ETHERCAT does not provide ", TransportType_Name(transport), " transport."));
        }
      }
      return absl::OkStatus();
    case CommType::ETHERNET_UDP:
      if (required != TransportSet{TransportType::MESSAGE}) {
        return absl::InvalidArgumentError("ETHERNET_UDP requires MESSAGE transport_type.");
      }
      return absl::UnimplementedError("ETHERNET_UDP communication is not implemented.");
    case CommType::COMM_INVALID:
    default:
      return absl::InvalidArgumentError("Comm has an invalid comm_type.");
  }
}

absl::StatusOr<CommLease> OpenComm(const robot::comm::Comm& comm,
                                   const TransportSet& required,
                                   const CommOptions& options) {
  CommCapabilities capabilities;
  switch (comm.comm_type()) {
    case CommType::SERIAL: {
      ABSL_ASSIGN_OR_RETURN(auto serial, CreateSerial(comm.serial_config()));
      if (required.count(TransportType::BYTE_STREAM) > 0) {
        capabilities.byte_stream = serial;
      }
      if (required.count(TransportType::MESSAGE) > 0) {
        capabilities.message =
            std::make_shared<SerialMessageTransport>(serial, options.message_framer);
      }
      return CommLease(std::move(capabilities));
    }
    case CommType::ETHERCAT: {
      ABSL_ASSIGN_OR_RETURN(capabilities.process_image,
                            CommFactory::CreateEthercat(comm.ethercat_config()));
      return CommLease(std::move(capabilities));
    }
    default:
      return absl::InternalError("Comm mechanism passed validation but cannot be opened.");
  }
}

bool Provides(const CommCapabilities& capabilities, TransportType transport) {
  switch (transport) {
    case TransportType::BYTE_STREAM:
      return capabilities.byte_stream != nullptr;
    case TransportType::MESSAGE:
      return capabilities.message != nullptr;
    case TransportType::CYCLIC:
      return capabilities.process_image != nullptr;
    case TransportType::CORRELATED_CYCLIC:
      return capabilities.correlated_cyclic != nullptr;
    default:
      return false;
  }
}
}  // namespace

absl::StatusOr<CommLease> CommFactory::Acquire(const robot::comm::Comm& comm,
                                               const CommOptions& options) {
  ABSL_ASSIGN_OR_RETURN(const TransportSet required, RequiredTransports(comm));
  ABSL_RETURN_IF_ERROR(ValidateMechanism(comm, required, options));

  CommLease lease;
  if (g_comm_lease_factory_for_testing) {
    ABSL_ASSIGN_OR_RETURN(lease, g_comm_lease_factory_for_testing(comm, options));
  } else {
    ABSL_ASSIGN_OR_RETURN(lease, OpenComm(comm, required, options));
  }
  for (const auto transport : required) {
    if (!Provides(lease.capabilities(), transport)) {
      return absl::InternalError(absl::StrCat(
          "Opened comm does not provide required ", TransportType_Name(transport), " transport."));
    }
  }
  return lease;
}

void CommFactory::SetCommLeaseFactoryForTesting(
    std::function<absl::StatusOr<CommLease>(const robot::comm::Comm&, const CommOptions&)>
        factory) {
  g_comm_lease_factory_for_testing = std::move(factory);
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
