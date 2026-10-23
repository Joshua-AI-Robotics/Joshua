#include "robot/board/factory/board_factory.h"

#include <map>
#include <mutex>
#include <string>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "robot/board/am243/am243_board.h"
#include "robot/board/esp32/esp32_board.h"
#include "robot/board/feetech_bus/feetech_bus_board.h"
#include "robot/board/mock/mock_board.h"
#include "robot/board/teensy/teensy_board.h"
#include "utils/status_macros.h"

namespace robot::board {

namespace {

struct CachedBoard {
  robot::board::BoardType board_type;
  std::shared_ptr<BoardInterface> board;
};

static std::mutex g_board_mutex;
static std::map<std::string, CachedBoard> g_boards;  // keyed by Board.name

absl::StatusOr<std::shared_ptr<BoardInterface>> CreateBoard(const robot::board::Board& config) {
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
       []() -> std::shared_ptr<BoardInterface> { return std::make_shared<Am243Board>(); }},
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
    return it->second.board;
  }

  ABSL_ASSIGN_OR_RETURN(auto board, CreateBoard(config));
  ABSL_RETURN_IF_ERROR(board->Init(config));
  g_boards[config.name()] = CachedBoard{config.board_type(), board};
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
