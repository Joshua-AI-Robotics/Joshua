#include "robot/comm/serial/joshua_wire_frame_stream.h"

#include <algorithm>
#include <condition_variable>
#include <deque>
#include <mutex>

#include "firmware/common/joshua_wire.h"
#include "firmware/common/joshua_wire_stream.h"

namespace robot::comm {
struct JoshuaWireFrameStream::State {
  struct QueuedFrame {
    ReceivedFrame frame;
    uint32_t session;
    uint32_t message;
    uint8_t function;
  };
  explicit State(SerialFrameOptions limits) : options(limits) {
    jw_stream_init_timeout(&decoder, options.frame_idle_timeout_ms);
  }
  SerialFrameOptions options;
  jw_stream_decoder_t decoder{};  // Accessed by the I/O worker only.
  mutable std::mutex mutex;
  std::condition_variable changed;
  std::deque<QueuedFrame> replies;
  std::deque<QueuedFrame> notifications;
  Diagnostics diagnostics;
  absl::Status failure;
  bool interrupted = false;
  void Stop(absl::Status status) {
    {
      std::lock_guard lock(mutex);
      if (failure.ok()) failure = std::move(status);
      replies.clear();
      notifications.clear();
      diagnostics.assembling_bytes = 0;
    }
    changed.notify_all();
  }
  absl::Status Feed(const uint8_t* bytes, size_t count) {
    const auto now = Clock::now();
    const auto ms = static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count());
    for (size_t i = 0; i < count; ++i) {
      if (decoder.used && uint32_t(ms - decoder.last_byte_ms) >= options.frame_idle_timeout_ms) {
        std::lock_guard lock(mutex);
        ++diagnostics.partial_frames_expired;
      }
      const int size = jw_stream_feed(&decoder, bytes[i], ms);
      if (size == 0) continue;
      std::unique_lock lock(mutex);
      if (!failure.ok()) return failure;
      if (size < 0) {
        ++diagnostics.malformed_frames;
        continue;
      }
      ++diagnostics.received_frames;
      jw_frame_t incoming{};
      jw_decode_frame(decoder.bytes, size, &incoming);
      QueuedFrame frame{{std::vector<uint8_t>(decoder.bytes, decoder.bytes + size), now},
                        incoming.session_id,
                        incoming.message_id,
                        incoming.channel};
      if (incoming.cmd == JW_CMD_STREAM_SAMPLE) {
        // Coalesce complete, unconsumed samples of the same identity only.
        auto same = std::find_if(notifications.begin(), notifications.end(), [&](const auto& old) {
          return old.session == incoming.session_id && old.message == incoming.message_id &&
                 old.function == incoming.channel;
        });
        if (same != notifications.end()) {
          // Keep arrival order after replacement, so later capacity eviction
          // removes the oldest current notification, not the oldest identity.
          notifications.erase(same);
          notifications.push_back(std::move(frame));
          ++diagnostics.notifications_dropped;
        } else {
          if (notifications.size() == options.max_notification_frames) {
            notifications.pop_front();
            ++diagnostics.notifications_dropped;
          }
          notifications.push_back(std::move(frame));
        }
      } else {
        if (replies.size() == options.max_reply_frames) {
          ++diagnostics.reply_overflows;
          failure =
              absl::ResourceExhaustedError("Reserved frame reply storage exhausted; link retired");
          replies.clear();
          notifications.clear();
          lock.unlock();
          changed.notify_all();
          return failure;
        }
        replies.push_back(std::move(frame));
      }
      lock.unlock();
      changed.notify_one();
    }
    {
      std::lock_guard lock(mutex);
      diagnostics.assembling_bytes = decoder.used;
    }
    return absl::OkStatus();
  }
};

absl::StatusOr<std::shared_ptr<JoshuaWireFrameStream>> JoshuaWireFrameStream::Open(
    const std::string& port,
    int baudrate,
    std::chrono::milliseconds settle,
    SerialIoOptions io,
    SerialFrameOptions frames) {
  auto valid = ValidateSerialFrameOptions(frames);
  if (!valid.ok()) return valid;
  if (io.max_transfer_bytes < JW_MAX_FRAME_LEN)
    return absl::InvalidArgumentError("Frame stream requires transfer limit >=96");
  auto state = std::make_shared<State>(frames);
  SerialIoOwner::Receiver receiver;
  receiver.bytes = [state](const uint8_t* bytes, size_t count) {
    return state->Feed(bytes, count);
  };
  receiver.stopped = [state](absl::Status status) { state->Stop(std::move(status)); };
  receiver.max_write_bytes = JW_MAX_FRAME_LEN;
  auto owner = SerialIoOwner::Open(port, baudrate, settle, io, std::move(receiver));
  if (!owner.ok()) return owner.status();
  return std::shared_ptr<JoshuaWireFrameStream>(
      new JoshuaWireFrameStream(state, std::move(*owner)));
}
JoshuaWireFrameStream::~JoshuaWireFrameStream() {
  Close().IgnoreError();
}
FrameStreamTransport::WriteResult JoshuaWireFrameStream::WriteFrame(absl::Span<const uint8_t> frame,
                                                                    Clock::time_point deadline) {
  jw_frame_t decoded{};
  if (jw_decode_frame(frame.data(), frame.size(), &decoded) != JW_RESULT_OK ||
      decoded.cmd == JW_CMD_STREAM_SAMPLE)
    return {absl::InvalidArgumentError("WriteFrame requires one valid host JoshuaWire frame"), 0};
  const auto now = Clock::now();
  if (deadline <= now) return {absl::DeadlineExceededError("Frame expired before submission"), 0};
  // Also retain the configured physical write cap; no attempt can exceed it.
  SerialIoOwner::Request request;
  request.bytes.assign(frame.begin(), frame.end());
  request.timeout = std::chrono::milliseconds(owner_->options().write_timeout_ms);
  request.deadline = deadline;
  auto result = owner_->Submit(std::move(request)).Wait();
  return {result.status, result.bytes_written};
}
absl::StatusOr<FrameStreamTransport::ReceivedFrame> JoshuaWireFrameStream::Receive(
    Clock::time_point deadline) {
  std::unique_lock lock(state_->mutex);
  if (!state_->changed.wait_until(lock, deadline, [&] {
        return !state_->failure.ok() || state_->interrupted || !state_->replies.empty() ||
               !state_->notifications.empty();
      }))
    return absl::DeadlineExceededError("Frame receive deadline");
  if (!state_->failure.ok()) return state_->failure;
  if (state_->interrupted) {
    state_->interrupted = false;
    return absl::CancelledError("Frame receiver interrupted for scheduling");
  }
  auto& queue = state_->replies.empty() ? state_->notifications : state_->replies;
  auto result = std::move(queue.front().frame);
  queue.pop_front();
  return result;
}
void JoshuaWireFrameStream::Interrupt() {
  {
    std::lock_guard lock(state_->mutex);
    state_->interrupted = true;
  }
  state_->changed.notify_one();
}
absl::Status JoshuaWireFrameStream::status() const {
  {
    std::lock_guard lock(state_->mutex);
    if (!state_->failure.ok()) return state_->failure;
  }
  return owner_->status();
}
absl::Status JoshuaWireFrameStream::Close() {
  state_->Stop(absl::CancelledError("Frame stream closed"));
  return owner_->Close();
}
FrameStreamTransport::Diagnostics JoshuaWireFrameStream::SnapshotFrames() const {
  std::lock_guard lock(state_->mutex);
  auto result = state_->diagnostics;
  result.replies_pending = state_->replies.size();
  result.notifications_pending = state_->notifications.size();
  return result;
}
}  // namespace robot::comm
