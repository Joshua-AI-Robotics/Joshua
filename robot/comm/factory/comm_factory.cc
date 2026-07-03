#include "robot/comm/factory/comm_factory.h"

#include <boost/asio.hpp>
#include <thread>

#include "robot/comm/ethercat/ethercat_transport.h"
#include "robot/comm/ethercat/soem_ethercat_transport.h"
#include "robot/comm/serial/serial.h"

namespace robot::comm {

namespace {
// Singleton-like caches for serial resources scoped to this TU
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
}  // namespace

absl::StatusOr<std::shared_ptr<Serial>> CommFactory::CreateSerial(const robot::comm::Comm& comm) {
  if (comm.comm_type() != CommType::SERIAL) {
    return absl::Status(absl::StatusCode::kInvalidArgument, "Comm is not of type SERIAL");
  }

  if (!comm.has_serial_config()) {
    return absl::Status(absl::StatusCode::kInvalidArgument, "Comm has no serial config");
  }

  if (comm.serial_config().port().empty()) {
    return absl::Status(absl::StatusCode::kInvalidArgument, "Serial config has no port");
  }

  if (comm.serial_config().baudrate() == 0) {
    return absl::Status(absl::StatusCode::kInvalidArgument, "Serial config has no baudrate");
  }

  const std::string& port = comm.serial_config().port();
  uint32_t baudrate = comm.serial_config().baudrate();

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

absl::StatusOr<std::shared_ptr<robot::comm::ethercat::EthercatTransport>>
CommFactory::CreateEthercatTransport(const robot::comm::Comm& comm) {
  if (comm.comm_type() != CommType::ETHERCAT) {
    return absl::Status(absl::StatusCode::kInvalidArgument, "Comm is not of type ETHERCAT");
  }

  if (!comm.has_ethercat_config()) {
    return absl::Status(absl::StatusCode::kInvalidArgument, "Comm has no EtherCAT config");
  }

  if (comm.ethercat_config().interface_name().empty()) {
    return absl::Status(absl::StatusCode::kInvalidArgument,
                        "EtherCAT config has no interface name");
  }

  auto process_data_mode_or =
      ToTransportProcessDataMode(comm.ethercat_config().process_data_mode());
  if (!process_data_mode_or.ok()) {
    return process_data_mode_or.status();
  }

  auto transport = std::make_shared<robot::comm::ethercat::SoemEthercatTransport>();
  auto status = transport->Init(comm.ethercat_config().interface_name(), *process_data_mode_or);
  if (!status.ok()) {
    return status;
  }
  return transport;
}
}  // namespace robot::comm
