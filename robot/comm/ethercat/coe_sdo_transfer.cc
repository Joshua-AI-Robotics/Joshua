// CoE wire fields are serialized explicitly; no compiler packing or SOEM types.
#include "robot/comm/ethercat/coe_sdo_transfer.h"

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace robot::comm::ethercat {
namespace {
constexpr uint16_t kSm0Status = 0x0805;
constexpr uint16_t kSm1Status = 0x080d;
constexpr uint8_t kMailboxFull = 0x08;
uint16_t U16(const uint8_t* p) {
  return p[0] | (uint16_t(p[1]) << 8);
}
uint32_t U32(const uint8_t* p) {
  return U16(p) | (uint32_t(U16(p + 2)) << 16);
}
void Put16(uint8_t* p, uint16_t value) {
  p[0] = value;
  p[1] = value >> 8;
}
void Put32(uint8_t* p, uint32_t value) {
  Put16(p, value);
  Put16(p + 2, value >> 16);
}
}  // namespace

absl::Status CoeSdoTransfer::Begin(Mailbox mailbox,
                                   uint16_t index,
                                   uint8_t subindex,
                                   uint8_t counter,
                                   bool write,
                                   Bytes bytes,
                                   size_t capacity) {
  if (phase_ != Phase::kIdle) return absl::FailedPreconditionError("SDO already active");
  const size_t size = write ? bytes.size() : capacity;
  // Deliberately reject segmented transfer. The object must fit in its mailbox
  // image, so a slave cannot force an unbounded segment loop.
  if (counter == 0 || counter > 7 || size == 0 || size > 76 || mailbox.write_size < 16 ||
      mailbox.read_size < 16 || mailbox.write_size > 1024 || mailbox.read_size > 1024 ||
      (write && size > 4 && size > size_t(mailbox.write_size - 16)) ||
      (!write && size > 4 && size > size_t(mailbox.read_size - 16)) || (!write && !bytes.empty()) ||
      (write && capacity != 0)) {
    return absl::InvalidArgumentError(
        "SDO requires a 1..76 byte unsegmented object and valid mailbox");
  }
  mailbox_ = mailbox;
  index_ = index;
  subindex_ = subindex;
  write_ = write;
  capacity_ = capacity;
  request_.assign(mailbox.write_size, 0);
  Put16(request_.data(), static_cast<uint16_t>(write && size > 4 ? 10 + size : 10));
  request_[5] = 0x03 | (counter << 4);  // CoE, mailbox counter.
  Put16(request_.data() + 6, 0x2000);   // SDO request service.
  request_[8] = write ? (size <= 4 ? uint8_t(0x23 | ((4 - size) << 2)) : 0x21) : 0x40;
  Put16(request_.data() + 9, index);
  request_[11] = subindex;
  if (write) {
    if (size > 4) Put32(request_.data() + 12, static_cast<uint32_t>(size));
    std::copy(bytes.begin(), bytes.end(), request_.begin() + (size <= 4 ? 12 : 16));
  }
  phase_ = Phase::kDrainStatus;
  return absl::OkStatus();
}

void CoeSdoTransfer::Cancel() {
  phase_ = Phase::kIdle;
  request_.clear();
}

absl::StatusOr<std::optional<CoeSdoTransfer::Bytes>> CoeSdoTransfer::Step(RegisterIo& io,
                                                                          int budget_us) {
  if (phase_ == Phase::kIdle) return absl::FailedPreconditionError("no active SDO");
  if (budget_us <= 0) return absl::InvalidArgumentError("SDO step needs positive budget");
  absl::Status status;
  switch (phase_) {
    case Phase::kDrainStatus:
    case Phase::kWriteStatus:
    case Phase::kReadStatus: {
      Bytes value(1, 0);
      status = io.Read(phase_ == Phase::kWriteStatus ? kSm0Status : kSm1Status, value, budget_us);
      if (status.ok() && value.size() != 1)
        status = absl::DataLossError("invalid mailbox status size");
      if (status.ok()) {
        const bool full = (value[0] & kMailboxFull) != 0;
        if (phase_ == Phase::kDrainStatus)
          phase_ = full ? Phase::kDrainRead : Phase::kWriteStatus;
        else if (phase_ == Phase::kWriteStatus && !full)
          phase_ = Phase::kWrite;
        else if (phase_ == Phase::kReadStatus && full)
          phase_ = Phase::kRead;
      }
      break;
    }
    case Phase::kDrainRead: {
      Bytes discarded(mailbox_.read_size);
      status = io.Read(mailbox_.read_offset, discarded, budget_us);
      phase_ = Phase::kDrainStatus;
      break;
    }
    case Phase::kWrite:
      status = io.Write(mailbox_.write_offset, request_, budget_us);
      phase_ = Phase::kReadStatus;
      break;
    case Phase::kRead: {
      Bytes response(mailbox_.read_size);
      status = io.Read(mailbox_.read_offset, response, budget_us);
      if (status.ok()) return Decode(response);
      break;
    }
    case Phase::kIdle:
      break;
  }
  if (!status.ok()) {
    Cancel();
    return status;
  }
  return std::optional<Bytes>{};
}

absl::StatusOr<std::optional<CoeSdoTransfer::Bytes>> CoeSdoTransfer::Decode(const Bytes& response) {
  if (response.size() != mailbox_.read_size) {
    Cancel();
    return absl::DataLossError("invalid mailbox response size");
  }
  phase_ = Phase::kReadStatus;
  // Mailbox counters belong to each sender independently; a response need not
  // echo the request counter (and zero disables counter checking). This path
  // neither retransmits writes nor requests mailbox repeats. It drains retained
  // mailboxes before sending and permits only one outstanding SDO. The owner
  // faults on a dispatched timeout, preventing a late reply from being reused.
  // Validate the CoE service/object below; JW2 correlation is checked above us.
  const size_t length = U16(response.data());
  if (length < 10 || length > response.size() - 6 || (response[5] & 0x0f) != 3 ||
      (U16(response.data() + 6) >> 12) != 3 || U16(response.data() + 9) != index_ ||
      response[11] != subindex_) {
    Cancel();
    return absl::DataLossError("malformed/mismatched CoE SDO response");
  }
  const uint8_t command = response[8];
  if (command == 0x80) {
    std::ostringstream message;
    message << "CoE SDO abort 0x" << std::hex << std::setw(8) << std::setfill('0')
            << U32(response.data() + 12) << " at 0x" << index_ << ':' << unsigned(subindex_);
    Cancel();
    return absl::FailedPreconditionError(message.str());
  }
  if (write_) {
    Cancel();
    if (command != 0x60 || length != 10)
      return absl::DataLossError("invalid SDO download acknowledgment");
    return std::optional<Bytes>(Bytes{});
  }
  size_t size = 0;
  size_t offset = 12;
  if ((command & 0xe3) == 0x43 && (command & 0x10) == 0 && length == 10) {
    size = 4 - ((command >> 2) & 3);
  } else if (command == 0x41) {
    size = U32(response.data() + 12);
    offset = 16;
    if (size != length - 10) {
      Cancel();
      return absl::UnimplementedError("segmented/inconsistent SDO response rejected");
    }
  } else {
    Cancel();
    return absl::DataLossError("unsupported SDO upload response");
  }
  if (size == 0 || size > capacity_ || offset + size > response.size()) {
    Cancel();
    return absl::DataLossError("SDO response exceeds requested capacity");
  }
  Bytes result(response.begin() + offset, response.begin() + offset + size);
  Cancel();
  return std::optional<Bytes>(std::move(result));
}

}  // namespace robot::comm::ethercat
