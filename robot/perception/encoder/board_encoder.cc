#include "robot/perception/encoder/board_encoder.h"

#include <chrono>
#include <utility>

#include "utils/status_macros.h"

namespace robot::perception {

BoardEncoder::BoardEncoder(std::shared_ptr<robot::board::BoardChannel> channel,
                           const robot::perception::Encoder& encoder_config)
    : channel_(std::move(channel)), id_(encoder_config.encoder_name()) {}

absl::Status BoardEncoder::Init() {
  // Nothing to do: BoardFactory::GetOrCreate already opened the port and
  // ran the board's IDENTIFY handshake (FeetechBusBoard pings each
  // configured servo) before handing over the channel. Re-reading here
  // would add a startup bus transaction that can only duplicate that
  // check, and a transient failure would drop the encoder for the whole
  // session -- per-read errors surface on the publisher timer instead.
  return absl::OkStatus();
}

absl::Status BoardEncoder::Teardown() {
  // The board owns the port and is shared with any actuator on the same
  // bus, so the encoder must not tear it down. BoardFactory owns board
  // lifetime.
  return absl::OkStatus();
}

std::string BoardEncoder::GetId() {
  return id_;
}

absl::StatusOr<float> BoardEncoder::GetPosition() {
  ABSL_ASSIGN_OR_RETURN(auto feedback, channel_->ReadFeedback());
  return feedback.position;
}

absl::StatusOr<robot::perception::PerceptionPacket> BoardEncoder::GetData() {
  ABSL_ASSIGN_OR_RETURN(auto position, GetPosition());
  reusable_packet_.Clear();
  reusable_packet_.set_perception_id(id_);
  reusable_packet_.set_timestamp_ns(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                        std::chrono::steady_clock::now().time_since_epoch())
                                        .count());
  reusable_packet_.mutable_position()->set_position(position);
  return reusable_packet_;
}

}  // namespace robot::perception
