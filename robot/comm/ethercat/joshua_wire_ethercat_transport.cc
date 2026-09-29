// Compatibility gate and paired CoE/PDO request lifecycle. Bus ownership stays
// in EthercatMaster; this worker only queues SDOs and consumes owned snapshots.
#include "robot/comm/ethercat/joshua_wire_ethercat_transport.h"

#include <algorithm>
#include <climits>
#include <sstream>
#include <utility>

#include "firmware/common/joshua_wire_ethercat.h"

namespace robot::comm::ethercat {
namespace {
using Bytes = EthercatMaster::Bytes;
using Microseconds = EthercatMaster::Microseconds;
constexpr uint32_t kRequired = JWEC_TRANSPORT_COE | JWEC_TRANSPORT_PDO;
uint16_t U16(const uint8_t* p) {
  return p[0] | (uint16_t(p[1]) << 8);
}
uint32_t U32(const uint8_t* p) {
  return U16(p) | (uint32_t(U16(p + 2)) << 16);
}
void Put16(uint8_t* p, uint16_t v) {
  p[0] = v;
  p[1] = v >> 8;
}
void Put32(uint8_t* p, uint32_t v) {
  Put16(p, v);
  Put16(p + 2, v >> 16);
}
bool ValidTimeout(Microseconds t) {
  return t.count() > 0 && t.count() <= INT_MAX;
}
absl::Status Expired() {
  return absl::DeadlineExceededError(
      "JW2 exchange cancelled/expired after dispatch; outcome unknown");
}
Bytes Envelope(bool pdo,
               uint32_t session,
               uint32_t generation,
               uint32_t ack,
               absl::Span<const uint8_t> frame = {}) {
  Bytes bytes(pdo ? JWEC_PDO_SIZE : JWEC_MAILBOX_SIZE, 0);
  Put32(bytes.data(), session);
  Put32(bytes.data() + 4, generation);
  if (pdo) Put32(bytes.data() + 8, ack);
  Put16(bytes.data() + (pdo ? JWEC_PDO_LENGTH_OFFSET : JWEC_MAILBOX_LENGTH_OFFSET), frame.size());
  std::copy(frame.begin(),
            frame.end(),
            bytes.begin() + (pdo ? JWEC_PDO_FRAME_OFFSET : JWEC_MAILBOX_FRAME_OFFSET));
  return bytes;
}
absl::StatusOr<Bytes> Response(absl::Span<const uint8_t> image,
                               bool pdo,
                               const jw2_frame_t& request) {
  const size_t offset = pdo ? JWEC_PDO_FRAME_OFFSET : JWEC_MAILBOX_FRAME_OFFSET;
  const size_t length = U16(image.data() + offset - 4);
  if (length < JW2_FRAME_OVERHEAD || length > JW2_MAX_FRAME_LEN ||
      U16(image.data() + offset - 2) != 0 ||
      !std::all_of(image.begin() + offset + length, image.end(), [](uint8_t v) { return v == 0; }))
    return absl::DataLossError("invalid JW2 envelope length, status/reserved bits or padding");
  jw2_frame_t response;
  if (jw2_decode_frame(image.data() + offset, length, &response) != 0 ||
      !jw2_response_matches(&request, &response))
    return absl::DataLossError("uncorrelated JW2 response; outcome unknown");
  return Bytes(image.begin() + offset, image.begin() + offset + length);
}
absl::StatusOr<absl::Span<const uint8_t>> Input(const EthercatMaster::Snapshot& snapshot,
                                                const PdoRegion& region) {
  const auto& bytes = snapshot.data.inputs;
  if (region.input_offset_bytes > bytes.size() ||
      JWEC_PDO_SIZE > bytes.size() - region.input_offset_bytes)
    return absl::DataLossError("truncated JW2 PDO snapshot");
  return absl::Span<const uint8_t>(bytes.data() + region.input_offset_bytes, JWEC_PDO_SIZE);
}
}  // namespace

absl::Status ValidateJoshuaWireEthercatProfile(absl::Span<const uint8_t> d,
                                               const PdoRegion& region,
                                               uint32_t required) {
  std::ostringstream observed;
  bool valid = d.size() == JWEC_DESCRIPTOR_SIZE;
  if (valid) {
    std::string artifact;
    bool padding = false;
    for (size_t i = 22; i < 34; ++i) {
      if (d[i] == 0) {
        padding = true;
        continue;
      }
      if (padding || d[i] < 32 || d[i] > 126) valid = false;
      artifact += d[i] >= 32 && d[i] <= 126 ? char(d[i]) : '?';
    }
    observed << "artifact='" << artifact << "' descriptor=" << U16(d.data() + 4)
             << " protocol=" << U16(d.data() + 6) << ".." << U16(d.data() + 8)
             << " layout=" << U16(d.data() + 10) << " PDO=" << U16(d.data() + 12) << '/'
             << U16(d.data() + 14) << " frame=" << U16(d.data() + 16)
             << " transports=" << U32(d.data() + 18);
    valid = valid && !artifact.empty() && d[0] == 'J' && d[1] == 'W' && d[2] == 'E' &&
            d[3] == 'C' && U16(d.data() + 4) == JWEC_DESCRIPTOR_VERSION && U16(d.data() + 6) <= 2 &&
            U16(d.data() + 8) >= 2 && U16(d.data() + 10) == JWEC_LAYOUT_VERSION &&
            U16(d.data() + 12) == JWEC_PDO_SIZE && U16(d.data() + 14) == JWEC_PDO_SIZE &&
            U16(d.data() + 16) == JW2_MAX_FRAME_LEN && U16(d.data() + 34) == 0 &&
            (U32(d.data() + 18) & required) == required;
  } else
    observed << "descriptor bytes=" << d.size() << " artifact=<unavailable>";
  valid = valid && (required & kRequired) == kRequired && (required & ~7u) == 0 &&
          region.output_size_bytes == JWEC_PDO_SIZE && region.input_size_bytes == JWEC_PDO_SIZE;
  if (valid) return absl::OkStatus();
  observed << "; mapped PDO=" << region.output_size_bytes << '/' << region.input_size_bytes
           << "; expected JWEC descriptor 1, protocol 2, layout 1, PDO 80/80, frame 64, transports="
           << required << ". Build and flash the matching JoshuaWire EtherCAT firmware artifact "
           << "separately; the TI echo demo is incompatible.";
  return absl::FailedPreconditionError(observed.str());
}

struct JoshuaWireEthercatTransport::Work {
  Bytes request;
  bool cyclic = false;
  bool dispatched = false;
  bool done = false;
  Clock::time_point deadline;
  absl::StatusOr<Bytes> result = absl::UnknownError("pending");
};

JoshuaWireEthercatTransport::JoshuaWireEthercatTransport(std::shared_ptr<EthercatMaster> master,
                                                         uint16_t slave,
                                                         Options options)
    : master_(std::move(master)),
      slave_(slave),
      region_(master_->regions()[slave - 1]),
      options_(options) {}

absl::StatusOr<std::shared_ptr<JoshuaWireEthercatTransport>> JoshuaWireEthercatTransport::Open(
    std::shared_ptr<EthercatMaster> master, uint16_t slave, Options options) {
  if (!master || slave == 0 || slave > master->regions().size() ||
      !ValidTimeout(options.exchange_timeout) || !ValidTimeout(options.poll_interval) ||
      options.poll_interval >= options.exchange_timeout)
    return absl::InvalidArgumentError("invalid JW2 endpoint, timeout or polling policy");
  auto status = master->ClaimEndpoint(slave);
  if (!status.ok()) return status;
  auto descriptor = master->ReadSdo(
      {slave, JWEC_DESCRIPTOR_INDEX, 0}, JWEC_DESCRIPTOR_SIZE, options.exchange_timeout);
  if (!descriptor.ok())
    return absl::Status(descriptor.status().code(),
                        std::string(descriptor.status().message()) +
                            "; cannot read JWEC descriptor/artifact. Build and flash matching "
                            "JoshuaWire EtherCAT firmware.");
  status = ValidateJoshuaWireEthercatProfile(*descriptor, master->regions()[slave - 1], kRequired);
  if (!status.ok()) return status;
  auto endpoint = std::shared_ptr<JoshuaWireEthercatTransport>(
      new JoshuaWireEthercatTransport(std::move(master), slave, options));
  endpoint->worker_ = std::thread(&JoshuaWireEthercatTransport::Run, endpoint.get());
  return endpoint;
}

JoshuaWireEthercatTransport::~JoshuaWireEthercatTransport() {
  Stop();
}
JoshuaWireEthercatTransport::Bytes JoshuaWireEthercatTransport::StopImage() {
  return Bytes(JWEC_PDO_SIZE, 0);
}
absl::Status JoshuaWireEthercatTransport::Send(absl::Span<const uint8_t>) {
  return absl::UnimplementedError("JW2 EtherCAT requires acknowledged Exchange, not Send");
}
absl::StatusOr<Bytes> JoshuaWireEthercatTransport::Exchange(absl::Span<const uint8_t> request) {
  return Submit(request, false, options_.exchange_timeout);
}
absl::StatusOr<Bytes> JoshuaWireEthercatTransport::Exchange(absl::Span<const uint8_t> request,
                                                            absl::Duration timeout) {
  if (timeout <= absl::ZeroDuration() || timeout > absl::Microseconds(INT_MAX))
    return absl::InvalidArgumentError("cyclic timeout must be finite, positive and <= INT_MAX us");
  return Submit(request, true, Microseconds(absl::ToInt64Microseconds(timeout)));
}

absl::StatusOr<Bytes> JoshuaWireEthercatTransport::Submit(absl::Span<const uint8_t> request,
                                                          bool cyclic,
                                                          Microseconds timeout) {
  jw2_frame_t frame;
  if (!ValidTimeout(timeout) || jw2_decode_frame(request.data(), request.size(), &frame) != 0)
    return absl::InvalidArgumentError("invalid JW2 frame or timeout");
  const bool cyclic_command = frame.cmd == JW_CMD_SET_TARGET || frame.cmd == JW_CMD_GET_FEEDBACK;
  const bool management = frame.cmd == JW_CMD_IDENTIFY || frame.cmd == JW_CMD_CONFIGURE_CHANNEL ||
                          frame.cmd == JW_CMD_ENABLE || frame.cmd == JW_CMD_DISABLE ||
                          frame.cmd == JW_CMD_ESTOP || frame.cmd == JW_CMD_RESET_SESSION;
  if ((cyclic && !cyclic_command) || (!cyclic && !management))
    return absl::InvalidArgumentError("JW2 command routed to wrong EtherCAT plane");
  if (frame.cmd == JW_CMD_RESET_SESSION &&
      (frame.channel != JW_CHANNEL_NONE || frame.payload_len != 0))
    return absl::InvalidArgumentError("invalid reset-session frame");
  auto work = std::make_shared<Work>();
  work->request.assign(request.begin(), request.end());
  work->cyclic = cyclic;
  work->deadline = Clock::now() + timeout;
  std::unique_lock lock(mutex_);
  if (stopping_) return absl::CancelledError("JW2 endpoint stopped");
  if (frame.cmd == JW_CMD_ESTOP) {
    // Drop queued normal work and cancel a cyclic waiter before queuing ESTOP.
    // An in-progress SDO is not interrupted/retried; its finite deadline applies.
    for (const auto& queued : queue_)
      FinishLocked(queued, absl::CancelledError("superseded by ESTOP"));
    queue_.clear();
    if (active_ && active_->cyclic)
      FinishLocked(active_, absl::CancelledError("ESTOP; outcome unknown"));
    (void)master_->SetOutputs(slave_, StopImage());
  }
  if (queue_.size() >= 64) return absl::ResourceExhaustedError("JW2 endpoint queue full");
  queue_.push_back(work);
  cv_.notify_all();
  if (!cv_.wait_until(lock, work->deadline, [&] { return work->done; })) {
    FinishLocked(work,
                 work->dispatched
                     ? Expired()
                     : absl::DeadlineExceededError("JW2 expired in queue; not executed"));
    if (work->dispatched && work->cyclic) (void)master_->SetOutputs(slave_, StopImage());
    cv_.notify_all();
  }
  return work->result;
}

void JoshuaWireEthercatTransport::FinishLocked(const std::shared_ptr<Work>& work,
                                               absl::StatusOr<Bytes> result) {
  if (work->done) return;
  work->result = std::move(result);
  work->done = true;
}
void JoshuaWireEthercatTransport::Stop() {
  {
    std::lock_guard lock(mutex_);
    stopping_ = true;
    if (active_) FinishLocked(active_, absl::CancelledError("JW2 stopped; outcome unknown"));
    for (const auto& work : queue_)
      FinishLocked(work, absl::CancelledError("JW2 stopped before dispatch"));
    queue_.clear();
    (void)master_->SetOutputs(slave_, StopImage());
  }
  cv_.notify_all();
  std::lock_guard lock(join_mutex_);
  if (worker_.joinable()) worker_.join();
}
Microseconds JoshuaWireEthercatTransport::Remaining(const std::shared_ptr<Work>& work) {
  std::lock_guard lock(mutex_);
  if (stopping_ || work->done) return Microseconds(0);
  return std::max(Microseconds(0),
                  std::chrono::duration_cast<Microseconds>(work->deadline - Clock::now()));
}
bool JoshuaWireEthercatTransport::Pause(const std::shared_ptr<Work>& work) {
  std::unique_lock lock(mutex_);
  cv_.wait_until(lock, std::min(work->deadline, Clock::now() + options_.poll_interval), [&] {
    return stopping_ || work->done;
  });
  return !stopping_ && !work->done && Clock::now() < work->deadline;
}
absl::Status JoshuaWireEthercatTransport::Publish(const std::shared_ptr<Work>& work, Bytes image) {
  std::lock_guard lock(mutex_);
  if (stopping_ || work->done || Clock::now() >= work->deadline) return Expired();
  // Shadow publication only, not bus I/O. The master never calls back into us.
  return master_->SetOutputs(slave_, std::move(image));
}
void JoshuaWireEthercatTransport::Invalidate() {
  std::lock_guard lock(mutex_);
  (void)master_->SetOutputs(slave_,
                            stopping_ ? StopImage() : Envelope(true, session_, 0, pdo_ack_));
}

void JoshuaWireEthercatTransport::Run() {
  while (true) {
    std::shared_ptr<Work> work;
    {
      std::unique_lock lock(mutex_);
      cv_.wait_for(lock, options_.poll_interval, [&] { return stopping_ || !queue_.empty(); });
      if (stopping_) break;
      if (!queue_.empty()) {
        work = queue_.front();
        queue_.pop_front();
        if (work->done) continue;
        if (Clock::now() >= work->deadline) {
          FinishLocked(work, absl::DeadlineExceededError("JW2 expired in queue; not executed"));
          cv_.notify_all();
          continue;
        }
        work->dispatched = true;
        active_ = work;
      }
    }
    if (!work) {
      DrainLatePdo();
      continue;
    }
    auto result = Execute(work);
    {
      std::lock_guard lock(mutex_);
      if (Clock::now() >= work->deadline) result = Expired();
      FinishLocked(work, std::move(result));
      active_.reset();
    }
    cv_.notify_all();
  }
}

absl::StatusOr<Bytes> JoshuaWireEthercatTransport::Execute(const std::shared_ptr<Work>& work) {
  jw2_frame_t frame;
  (void)jw2_decode_frame(work->request.data(), work->request.size(), &frame);
  if (frame.cmd == JW_CMD_RESET_SESSION) return Reset(work);
  if (needs_reset_ || frame.session_id != session_)
    return absl::FailedPreconditionError("JW2 EtherCAT requires a new verified reset session");
  if (frame.message_id <= last_message_)
    return absl::FailedPreconditionError("JW2 message ID reused or out of order");
  if (generation_ == UINT32_MAX || (generation_ == UINT32_MAX - 1 && frame.cmd != JW_CMD_ESTOP))
    return absl::ResourceExhaustedError(
        "JW2 generation exhausted; disable and establish new session");
  last_message_ = frame.message_id;
  const auto generation = ++generation_;
  auto result = work->cyclic ? Cyclic(work, generation) : Mailbox(work, generation);
  if (work->cyclic) {
    Invalidate();
    if (absl::IsDataLoss(result.status())) needs_reset_ = true;
  } else if (!result.ok())
    needs_reset_ = true;
  return result;
}

absl::StatusOr<Bytes> JoshuaWireEthercatTransport::Reset(const std::shared_ptr<Work>& work) {
  jw2_frame_t frame;
  (void)jw2_decode_frame(work->request.data(), work->request.size(), &frame);
  if (frame.session_id == session_)
    return absl::FailedPreconditionError("reset must propose a different nonzero session");
  needs_reset_ = true;
  Invalidate();
  auto remaining = Remaining(work);
  if (remaining.count() <= 0) return Expired();
  Bytes object(JWEC_SESSION_SIZE, 0);
  Put32(object.data(), JWEC_RESET_OPERATION);
  Put32(object.data() + 4, frame.session_id);
  auto status = master_->WriteSdo({slave_, JWEC_SESSION_INDEX, 0}, object, remaining);
  if (!status.ok()) return status;
  while ((remaining = Remaining(work)).count() > 0) {
    auto reply = master_->ReadSdo({slave_, JWEC_SESSION_INDEX, 0}, JWEC_SESSION_SIZE, remaining);
    if (!reply.ok()) return reply.status();
    if (reply->size() != JWEC_SESSION_SIZE) return absl::DataLossError("invalid reset object size");
    if (*reply == object) {
      // The object readback is the reset acknowledgment. Firmware publishes it
      // only after disabling outputs and clearing both planes' retained state.
      if (Remaining(work).count() <= 0) return Expired();
      session_ = frame.session_id;
      last_message_ = frame.message_id;
      generation_ = 0;
      pdo_ack_ = 0;
      needs_reset_ = false;
      Invalidate();
      Bytes response(JW2_MAX_FRAME_LEN);
      const uint8_t ok = JW_STATUS_OK;
      const int size = jw2_encode_response(response.data(), response.size(), &frame, &ok, 1);
      response.resize(size);
      return response;
    }
    if (!Pause(work)) break;
  }
  return Expired();
}

absl::StatusOr<Bytes> JoshuaWireEthercatTransport::Mailbox(const std::shared_ptr<Work>& work,
                                                           uint32_t generation) {
  jw2_frame_t frame;
  (void)jw2_decode_frame(work->request.data(), work->request.size(), &frame);
  // Reset may precede StartCyclic, when there is no output shadow to publish.
  // Before management enables anything, replace the startup all-zero stop
  // image with this session's idle image. ESTOP keeps the explicit stop image.
  if (frame.cmd != JW_CMD_ESTOP) Invalidate();
  auto remaining = Remaining(work);
  if (remaining.count() <= 0) return Expired();
  auto status = master_->WriteSdo({slave_, JWEC_REQUEST_INDEX, 0},
                                  Envelope(false, session_, generation, 0, work->request),
                                  remaining);
  if (!status.ok()) return status;
  while ((remaining = Remaining(work)).count() > 0) {
    auto reply = master_->ReadSdo({slave_, JWEC_RESPONSE_INDEX, 0}, JWEC_MAILBOX_SIZE, remaining);
    if (!reply.ok()) return reply.status();
    if (reply->size() != JWEC_MAILBOX_SIZE)
      return absl::DataLossError("invalid mailbox envelope size");
    const uint32_t response_generation = U32(reply->data() + 4);
    if (U32(reply->data()) == session_ && response_generation != 0) {
      if (response_generation > generation) return absl::DataLossError("future mailbox generation");
      absl::StatusOr<Bytes> response = Bytes{};
      if (response_generation == generation) response = Response(*reply, false, frame);
      Bytes ack(4);
      Put32(ack.data(), response_generation);
      remaining = Remaining(work);
      if (remaining.count() <= 0) return Expired();
      status = master_->WriteSdo({slave_, JWEC_ACK_INDEX, 0}, ack, remaining);
      if (!status.ok()) return status;
      if (response_generation == generation) return response;
    }
    if (!Pause(work)) break;
  }
  return Expired();
}

absl::StatusOr<Bytes> JoshuaWireEthercatTransport::Cyclic(const std::shared_ptr<Work>& work,
                                                          uint32_t generation) {
  auto status = Publish(work, Envelope(true, session_, generation, pdo_ack_, work->request));
  if (!status.ok()) return status;
  jw2_frame_t frame;
  (void)jw2_decode_frame(work->request.data(), work->request.size(), &frame);
  Microseconds remaining;
  bool saw_session = false;
  while ((remaining = Remaining(work)).count() > 0) {
    auto snapshot = master_->WaitForCycle(sequence_, std::min(remaining, options_.poll_interval));
    if (!snapshot.ok()) {
      if (absl::IsDeadlineExceeded(snapshot.status()) && master_->status().ok()) continue;
      return snapshot.status();
    }
    sequence_ = snapshot->sequence;
    auto input = Input(*snapshot, region_);
    if (!input.ok()) return input.status();
    const auto bytes = *input;
    if (U32(bytes.data()) != session_) {
      if (U32(bytes.data()) == 0) {
        // A pre-publication snapshot may contain the startup/old image. Require
        // an echoed output from this request before treating zero as a reboot.
        const auto& out = snapshot->data.outputs;
        if (saw_session || (region_.output_offset_bytes <= out.size() &&
                            JWEC_PDO_SIZE <= out.size() - region_.output_offset_bytes &&
                            U32(out.data() + region_.output_offset_bytes + 4) == generation)) {
          needs_reset_ = true;
          return absl::FailedPreconditionError("firmware session lost; reset required");
        }
      }
      continue;
    }
    saw_session = true;
    const uint32_t accepted = U32(bytes.data() + 4);
    const uint32_t response_generation = U32(bytes.data() + 8);
    if (U16(bytes.data() + 14) != JWEC_TRANSPORT_STATUS_OK)
      return absl::DataLossError("firmware reported PDO transport error");
    if (accepted > generation || response_generation > generation)
      return absl::DataLossError("future PDO generation");
    if (response_generation != 0) {
      pdo_ack_ = std::max(pdo_ack_, response_generation);
      if (response_generation == generation) {
        if (accepted != generation) return absl::DataLossError("PDO response without acceptance");
        return Response(bytes, true, frame);
      }
    }
    // Clear validity after acceptance, retaining the response acknowledgment.
    status = Publish(work,
                     Envelope(true,
                              session_,
                              accepted == generation ? 0 : generation,
                              pdo_ack_,
                              accepted == generation ? absl::Span<const uint8_t>{}
                                                     : absl::Span<const uint8_t>(work->request)));
    if (!status.ok()) return status;
  }
  return Expired();
}

void JoshuaWireEthercatTransport::DrainLatePdo() {
  if (session_ == 0 || needs_reset_) return;
  auto snapshot = master_->WaitForCycle(sequence_, Microseconds(1));
  if (!snapshot.ok()) return;  // No cyclic startup/new snapshot, or master stopped.
  sequence_ = snapshot->sequence;
  auto input = Input(*snapshot, region_);
  if (!input.ok() || U32(input->data()) != session_) return;
  const uint32_t generation = U32(input->data() + 8);
  if (generation != 0 && generation <= generation_) {
    pdo_ack_ = std::max(pdo_ack_, generation);
    Invalidate();  // A late response is acknowledged, never delivered to a caller.
  }
}

}  // namespace robot::comm::ethercat
