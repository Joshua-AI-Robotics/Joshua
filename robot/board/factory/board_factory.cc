#include "robot/board/factory/board_factory.h"

#include <map>
#include <mutex>
#include <string>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "firmware/common/joshua_wire_commands.h"
#include "robot/board/esp32/esp32_board.h"
#include "robot/board/feetech_bus/feetech_bus_board.h"
#include "robot/board/joshua_wire/joshua_wire_board.h"
#include "robot/board/mock/mock_board.h"
#include "robot/board/teensy/teensy_board.h"
#include "robot/comm/serial/serial_device_identity.h"
#include "utils/status_macros.h"

namespace robot::board {

namespace {

struct CachedBoard {
  robot::board::BoardType board_type;
  std::string config_key;
  std::shared_ptr<BoardInterface> board;
  std::string joshua_serial_identity;
};

static std::mutex g_board_mutex;
static std::map<std::string, CachedBoard> g_boards;  // keyed by Board.name

std::string JoshuaSerialPort(const robot::board::Board& config) {
  const auto type = config.board_type();
  if ((type == AM243 || type == TEENSY41 || type == ESP32) &&
      config.comm().comm_type() == robot::comm::SERIAL &&
      config.comm().serial_config().message_adapter() == robot::comm::JOSHUA_WIRE)
    return config.comm().serial_config().port();
  return {};
}

absl::StatusOr<std::shared_ptr<BoardInterface>> CreateBoard(const robot::board::Board& config) {
  if (config.board_type() == AM243 &&
      (config.has_am243_config() ||
       (config.comm().comm_type() == robot::comm::ETHERCAT &&
        (config.protocol() != JOSHUA_WIRE ||
         config.comm().transport_type() != robot::comm::MESSAGE_AND_CYCLIC)))) {
    return absl::InvalidArgumentError(
        "Legacy AM243 TI-demo path is retired; select explicit JW with "
        "MESSAGE_AND_CYCLIC and comm.ethercat_config endpoint/timing fields.");
  }
  for (const auto& implementation : BoardFactory::Implementations()) {
    if (implementation.type != config.board_type()) continue;
    if (implementation.create) return implementation.create();
    return absl::UnimplementedError(
        absl::StrCat("Board '",
                     config.name(),
                     "': board_type ",
                     BoardType_Name(config.board_type()),
                     " is not implemented yet (docs/BOARD_LAYER_RFC.md §10)."));
  }
  return absl::InvalidArgumentError(
      absl::StrCat("Board '", config.name(), "' has an invalid board_type."));
}

}  // namespace

const std::vector<BoardImplementation>& BoardFactory::Implementations() {
  static const std::vector<BoardImplementation> entries = {
      {MOCK,
       "test_only",
       []() -> std::shared_ptr<BoardInterface> { return std::make_shared<MockBoard>(); }},
      {AM243,
       "installed",
       []() -> std::shared_ptr<BoardInterface> {
         return std::make_shared<JoshuaWireBoard>(AM243);
       }},
      {FEETECH_BUS,
       "installed",
       []() -> std::shared_ptr<BoardInterface> { return std::make_shared<FeetechBusBoard>(); }},
      {TEENSY41,
       "installed",
       []() -> std::shared_ptr<BoardInterface> { return std::make_shared<TeensyBoard>(); }},
      {ESP32,
       "installed",
       []() -> std::shared_ptr<BoardInterface> { return std::make_shared<Esp32Board>(); }},
      {ARDUINO_UNO, "unimplemented", nullptr},
      {HOST_GPIO, "unimplemented", nullptr}};
  return entries;
}

absl::StatusOr<std::shared_ptr<BoardInterface>> BoardFactory::GetOrCreate(
    const robot::board::Board& config) {
  if (config.name().empty()) {
    return absl::InvalidArgumentError("Board config has no name; boards are cached by name.");
  }

  std::lock_guard<std::mutex> lock(g_board_mutex);
  const auto port = JoshuaSerialPort(config);
  auto resolved_config = config;
  auto key_config = config;
  std::string serial_identity;
  if (!port.empty()) {
    ABSL_ASSIGN_OR_RETURN(const auto device, robot::comm::ResolveSerialDeviceIdentity(port));
    serial_identity = device.key;
    resolved_config.mutable_comm()->mutable_serial_config()->set_port(device.path);
    key_config.mutable_comm()->mutable_serial_config()->set_port(device.key);
  }
  const auto config_key = key_config.SerializeAsString();
  auto it = g_boards.find(config.name());
  if (it != g_boards.end()) {
    if (it->second.board_type != config.board_type()) {
      return absl::InvalidArgumentError(
          absl::StrCat("Board '",
                       config.name(),
                       "' is already cached as ",
                       robot::board::BoardType_Name(it->second.board_type),
                       " but this config declares ",
                       robot::board::BoardType_Name(config.board_type()),
                       "; two boards may not share one name."));
    }
    if (it->second.config_key != config_key)
      return absl::InvalidArgumentError(
          "Board name is already cached with a different configuration.");
    return it->second.board;
  }

  if (!serial_identity.empty()) {
    for (const auto& [name, cached] : g_boards) {
      if (cached.joshua_serial_identity == serial_identity)
        return absl::AlreadyExistsError(absl::StrCat(
            "JoshuaWire serial port '",
            port,
            "' already belongs to board '",
            name,
            "'; reference that board's channels instead of creating a second session."));
    }
  }
  ABSL_ASSIGN_OR_RETURN(auto board, CreateBoard(config));
  ABSL_RETURN_IF_ERROR(board->Init(resolved_config));
  g_boards[config.name()] = CachedBoard{config.board_type(), config_key, board, serial_identity};
  return board;
}

void BoardFactory::ResetForTesting() {
  std::lock_guard<std::mutex> lock(g_board_mutex);
  for (auto& [name, cached] : g_boards) {
    cached.board->Teardown().IgnoreError();
  }
  g_boards.clear();
}

}  // namespace robot::board
