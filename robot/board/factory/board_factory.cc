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
#include "utils/status_macros.h"

namespace robot::board {

namespace {

struct CachedBoard {
  robot::board::BoardType board_type;
  std::string config_key;
  std::shared_ptr<BoardInterface> board;
};

static std::mutex g_board_mutex;
static std::map<std::string, CachedBoard> g_boards;  // keyed by Board.name

absl::StatusOr<std::shared_ptr<BoardInterface>> CreateBoard(const robot::board::Board& config) {
  switch (config.board_type()) {
    case robot::board::BoardType::MOCK:
      return std::make_shared<MockBoard>();
    case robot::board::BoardType::AM243:
      if (config.has_am243_config() ||
          (config.comm().comm_type() == robot::comm::ETHERCAT &&
           (config.protocol() != JOSHUA_WIRE_V2 ||
            config.comm().transport_type() != robot::comm::MESSAGE_AND_CYCLIC)))
        return absl::InvalidArgumentError(
            "Legacy AM243 TI-demo path is retired; select explicit JW2 with "
            "MESSAGE_AND_CYCLIC and comm.ethercat_config endpoint/timing fields.");
      return std::make_shared<JoshuaWireBoard>(AM243, JW_BOARD_AM243);
    case robot::board::BoardType::FEETECH_BUS:
      return std::make_shared<FeetechBusBoard>();
    case robot::board::BoardType::TEENSY41:
      return std::make_shared<TeensyBoard>();
    case robot::board::BoardType::ESP32:
      return std::make_shared<Esp32Board>();
    case robot::board::BoardType::ARDUINO_UNO:
    case robot::board::BoardType::HOST_GPIO:
      return absl::UnimplementedError(
          absl::StrCat("Board '",
                       config.name(),
                       "': board_type ",
                       robot::board::BoardType_Name(config.board_type()),
                       " is not implemented yet (docs/BOARD_LAYER_RFC.md §10)."));
    case robot::board::BoardType::BOARD_INVALID:
    default:
      return absl::InvalidArgumentError(
          absl::StrCat("Board '", config.name(), "' has an invalid board_type."));
  }
}

}  // namespace

absl::StatusOr<std::shared_ptr<BoardInterface>> BoardFactory::GetOrCreate(
    const robot::board::Board& config) {
  if (config.name().empty()) {
    return absl::InvalidArgumentError("Board config has no name; boards are cached by name.");
  }

  std::lock_guard<std::mutex> lock(g_board_mutex);
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
    if (it->second.config_key != config.SerializeAsString())
      return absl::InvalidArgumentError(
          "Board name is already cached with a different configuration.");
    return it->second.board;
  }

  ABSL_ASSIGN_OR_RETURN(auto board, CreateBoard(config));
  ABSL_RETURN_IF_ERROR(board->Init(config));
  g_boards[config.name()] = CachedBoard{config.board_type(), config.SerializeAsString(), board};
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
