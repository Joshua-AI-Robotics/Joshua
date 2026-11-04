#pragma once
#include <memory>
#include <string>

#include "robot/comm/interfaces/frame_stream_transport.h"
#include "robot/comm/serial/serial_frame_options.h"
#include "robot/comm/serial/serial_io_owner.h"

namespace robot::comm {
class JoshuaWireFrameStream final : public FrameStreamTransport {
 public:
  static absl::StatusOr<std::shared_ptr<JoshuaWireFrameStream>> Open(
      const std::string& port,
      int baudrate,
      std::chrono::milliseconds settle,
      SerialIoOptions io,
      SerialFrameOptions frames);
  ~JoshuaWireFrameStream() override;
  JoshuaWireFrameStream(const JoshuaWireFrameStream&) = delete;
  JoshuaWireFrameStream& operator=(const JoshuaWireFrameStream&) = delete;
  WriteResult WriteFrame(absl::Span<const uint8_t> frame, Clock::time_point deadline) override;
  absl::StatusOr<ReceivedFrame> Receive(Clock::time_point deadline) override;
  void Interrupt() override;
  absl::Status status() const override;
  absl::Status Close() override;
  Diagnostics SnapshotFrames() const override;
  SerialDiagnostics Snapshot() const {
    return owner_->Snapshot();
  }
  const SerialIoOwner::PortIdentity& identity() const {
    return owner_->identity();
  }

 private:
  struct State;
  JoshuaWireFrameStream(std::shared_ptr<State> state, std::shared_ptr<SerialIoOwner> owner)
      : state_(std::move(state)), owner_(std::move(owner)) {}
  std::shared_ptr<State> state_;
  std::shared_ptr<SerialIoOwner> owner_;
};
}  // namespace robot::comm
