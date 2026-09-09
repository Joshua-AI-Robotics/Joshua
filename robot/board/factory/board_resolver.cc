#include "robot/board/factory/board_resolver.h"

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"

namespace robot::board {

absl::StatusOr<ResolvedChannelConfig> ResolveChannelConfig(
    const google::protobuf::RepeatedPtrField<robot::board::Board>& boards,
    absl::string_view owner,
    absl::string_view board_name,
    uint32_t channel_index) {
  if (board_name.empty()) {
    return absl::InvalidArgumentError(absl::StrCat(owner, " has no board_name."));
  }

  const robot::board::Board* board_config = nullptr;
  for (const auto& board : boards) {
    if (board.name() == board_name) {
      board_config = &board;
      break;
    }
  }
  if (board_config == nullptr) {
    return absl::NotFoundError(absl::StrCat(
        owner, " references board '", board_name, "' but no boards{} entry declares that name."));
  }

  const robot::board::Channel* channel_config = nullptr;
  for (const auto& channel : board_config->channels()) {
    if (channel.index() == channel_index) {
      channel_config = &channel;
      break;
    }
  }
  if (channel_config == nullptr) {
    return absl::NotFoundError(absl::StrCat(owner,
                                            " uses channel ",
                                            channel_index,
                                            " but board '",
                                            board_config->name(),
                                            "' does not declare it."));
  }

  return ResolvedChannelConfig{.board = board_config, .channel = channel_config};
}

}  // namespace robot::board
