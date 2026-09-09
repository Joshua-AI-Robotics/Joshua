#pragma once

#include <cstdint>
#include <string>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "google/protobuf/repeated_ptr_field.h"
#include "robot/board/proto/board.pb.h"

namespace robot::board {

// A board and one of its channels, both resolved from config.
struct ResolvedChannelConfig {
  const robot::board::Board* board = nullptr;
  const robot::board::Channel* channel = nullptr;
};

// Resolves (board_name, channel) against boards{}, the single implementation
// of the lookup both ActionFactory and PerceptionFactory need
// (docs/BOARD_LAYER_RFC.md §6.5). `owner` names the thing doing the
// referencing — "Actuator 'servo_1'", "Sensor 'joint_1'" — and appears in
// every error, so a config mistake says which entry is wrong.
//
// The returned pointers alias into `boards`; they are valid as long as it is.
absl::StatusOr<ResolvedChannelConfig> ResolveChannelConfig(
    const google::protobuf::RepeatedPtrField<robot::board::Board>& boards,
    absl::string_view owner,
    absl::string_view board_name,
    uint32_t channel_index);

}  // namespace robot::board
