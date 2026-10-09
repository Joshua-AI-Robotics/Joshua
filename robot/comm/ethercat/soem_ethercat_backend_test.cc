#include "robot/comm/ethercat/soem_ethercat_backend.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "absl/status/status.h"
#include "firmware/common/joshua_wire_ethercat.h"
#include "gtest/gtest.h"
#include "robot/comm/ethercat/coe_sdo_transfer.h"
#include "robot/comm/ethercat/ethercat_master.h"
#include "robot/comm/ethercat/ethercat_types.h"

namespace robot::comm::ethercat {
namespace {

TEST(SoemEthercatBackendTest, InitRejectsEmptyInterfaceName) {
  SoemEthercatBackend transport;

  auto status = transport.Init("", ProcessDataMode::kSplitLrdLwr);

  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
}

TEST(SoemEthercatBackendTest, InitRejectsLrwMode) {
  SoemEthercatBackend transport;

  auto status = transport.Init("enp5s0", ProcessDataMode::kLrw);

  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
}

TEST(SoemEthercatBackendTest, InitReportsUnavailableForMissingInterface) {
  SoemEthercatBackend transport;

  auto status = transport.Init("joshua-no-such-ethercat-iface0", ProcessDataMode::kSplitLrdLwr);

  EXPECT_EQ(status.code(), absl::StatusCode::kUnavailable);
}

TEST(SoemEthercatBackendTest, TeardownIsOkBeforeInit) {
  SoemEthercatBackend transport;

  EXPECT_TRUE(transport.Teardown().ok());
}

TEST(SoemEthercatBackendTest, ConfigureSlavesRequiresInit) {
  SoemEthercatBackend transport;

  EXPECT_EQ(transport.ConfigureSlaves().code(), absl::StatusCode::kFailedPrecondition);
}

TEST(SoemEthercatBackendTest, StartCyclicRequiresConfiguredSlaves) {
  SoemEthercatBackend transport;

  EXPECT_EQ(transport.StartCyclic().code(), absl::StatusCode::kFailedPrecondition);
}

TEST(SoemEthercatBackendTest, MetadataAccessRequiresConfiguredSlaves) {
  SoemEthercatBackend transport;

  EXPECT_EQ(transport.GetSlaves().status().code(), absl::StatusCode::kFailedPrecondition);
  EXPECT_EQ(transport.GetPdoRegion(2).status().code(), absl::StatusCode::kFailedPrecondition);
}

TEST(SoemEthercatBackendTest, PdoBufferAccessRequiresConfiguredSlaves) {
  SoemEthercatBackend transport;
  PdoRegion region;
  region.slave_index = 2;
  region.output_size_bytes = 8;
  region.input_size_bytes = 8;

  EXPECT_EQ(transport.WriteOutputs(region, std::vector<uint8_t>(8, 0)).code(),
            absl::StatusCode::kFailedPrecondition);
}

TEST(SoemEthercatBackendTest, ExchangeProcessDataRequiresConfiguredSlaves) {
  SoemEthercatBackend transport;

  EXPECT_EQ(transport.ExchangeProcessData(1000).status().code(),
            absl::StatusCode::kFailedPrecondition);
}

TEST(SoemEthercatBackendTest, StopCyclicIsOkBeforeInit) {
  SoemEthercatBackend transport;

  EXPECT_TRUE(transport.StopCyclic().ok());
}

TEST(SoemEthercatBackendTest, NewIoMethodsFailBeforeAccessingHardware) {
  SoemEthercatBackend transport;
  EXPECT_EQ(transport.ReadSdo({1, 0x2000, 0}, 36, 1000).status().code(),
            absl::StatusCode::kFailedPrecondition);
  EXPECT_EQ(transport.WriteSdo({1, 0x2001, 0}, {1}, 1000).code(),
            absl::StatusCode::kFailedPrecondition);
  EXPECT_EQ(transport.CheckOperational(1000).code(), absl::StatusCode::kFailedPrecondition);
  EXPECT_EQ(transport.ExchangeProcessData(0).status().code(), absl::StatusCode::kInvalidArgument);
}

// Owner-loop regressions stay alongside backend tests. This test-local backend
// never opens a NIC; finite gates simulate slow I/O without risking hung tests.
using namespace std::chrono_literals;
struct IoTrace {
  std::mutex mutex;
  std::condition_variable cv;
  std::vector<std::string> calls;
  std::vector<std::thread::id> threads;
  std::vector<uint8_t> outputs = {0, 0, 0, 0};
  std::vector<std::vector<uint8_t>> sent;
  std::string hold;
  bool entered = false;
  bool released = false;
  bool fail_init = false;
  std::atomic<bool> bad_wkc{false};
  std::atomic<bool> not_operational{false};
  int sdo_calls = 0;
  int sdo_timeout = 0;
  SdoAddress last_address{};
  std::vector<uint8_t> last_write;

  void Call(const std::string& name) {
    std::unique_lock lock(mutex);
    calls.push_back(name);
    threads.push_back(std::this_thread::get_id());
    if (hold == name && !released) {
      entered = true;
      cv.notify_all();
      cv.wait_for(lock, 2s, [&] { return released; });
    }
  }
  bool WaitEntered() {
    std::unique_lock lock(mutex);
    return cv.wait_for(lock, 1s, [&] { return entered; });
  }
  void Release() {
    {
      std::lock_guard lock(mutex);
      released = true;
    }
    cv.notify_all();
  }
};

class TestMasterIo : public EthercatMasterIo {
 public:
  explicit TestMasterIo(std::shared_ptr<IoTrace> trace) : trace_(std::move(trace)) {}
  ~TestMasterIo() override {
    trace_->Call("destroy");
  }
  absl::Status Init(const std::string&, ProcessDataMode) override {
    trace_->Call("init");
    return trace_->fail_init ? absl::UnavailableError("no bus") : absl::OkStatus();
  }
  absl::Status ConfigureSlaves() override {
    trace_->Call("configure");
    return absl::OkStatus();
  }
  absl::Status StartCyclic() override {
    trace_->Call("start");
    return absl::OkStatus();
  }
  absl::Status StopCyclic() override {
    trace_->Call("stop");
    return absl::OkStatus();
  }
  absl::Status Teardown() override {
    trace_->Call("teardown");
    return absl::OkStatus();
  }
  absl::StatusOr<std::vector<SlaveIdentity>> GetSlaves() const override {
    trace_->Call("slaves");
    return std::vector<SlaveIdentity>(2);
  }
  absl::StatusOr<PdoRegion> GetPdoRegion(uint16_t slave) const override {
    trace_->Call("region");
    return PdoRegion{
        slave, static_cast<size_t>((slave - 1) * 2), static_cast<size_t>((slave - 1) * 2), 2, 2};
  }
  absl::Status WriteOutputs(const PdoRegion& region, const std::vector<uint8_t>& bytes) override {
    trace_->Call("outputs");
    std::copy(bytes.begin(), bytes.end(), trace_->outputs.begin() + region.output_offset_bytes);
    return absl::OkStatus();
  }
  absl::StatusOr<ProcessData> ExchangeProcessData(int timeout_us) override {
    EXPECT_GT(timeout_us, 0);
    trace_->Call("exchange");
    trace_->sent.push_back(trace_->outputs);
    return ProcessData{trace_->outputs, trace_->outputs, 6, trace_->bad_wkc ? 0 : 6};
  }
  absl::Status CheckOperational(int timeout_us) override {
    EXPECT_GT(timeout_us, 0);
    trace_->Call("state");
    return trace_->not_operational ? absl::UnavailableError("not OP") : absl::OkStatus();
  }
  absl::StatusOr<std::vector<uint8_t>> ReadSdo(SdoAddress address,
                                               size_t,
                                               int timeout_us) override {
    trace_->Call("sdo");
    ++trace_->sdo_calls;
    trace_->sdo_timeout = timeout_us;
    trace_->last_address = address;
    return std::vector<uint8_t>{0x12, 0x34};
  }
  absl::Status WriteSdo(SdoAddress address,
                        const std::vector<uint8_t>& bytes,
                        int timeout_us) override {
    trace_->Call("sdo");
    ++trace_->sdo_calls;
    trace_->sdo_timeout = timeout_us;
    trace_->last_address = address;
    trace_->last_write = bytes;
    return absl::OkStatus();
  }

 private:
  std::shared_ptr<IoTrace> trace_;
};

EthercatMaster::Options MasterOptions() {
  return {50ms, 2ms, 2ms, 1s};
}
auto OpenMaster(const std::shared_ptr<IoTrace>& trace,
                EthercatMaster::Options options = MasterOptions()) {
  return EthercatMaster::Open(
      std::make_unique<TestMasterIo>(trace), "test-only", ProcessDataMode::kSplitLrdLwr, options);
}

TEST(EthercatMasterTest, RejectsImpossibleTimingBeforeInit) {
  auto trace = std::make_shared<IoTrace>();
  auto options = MasterOptions();
  options.period = options.process_timeout + options.state_timeout;
  EXPECT_EQ(OpenMaster(trace, options).status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(trace->calls, (std::vector<std::string>{"destroy"}));
  options = MasterOptions();
  options.operation_timeout = 0us;
  EXPECT_EQ(OpenMaster(trace, options).status().code(), absl::StatusCode::kInvalidArgument);
}

TEST(EthercatMasterTest, FailedStartupTearsDownOnOwner) {
  auto trace = std::make_shared<IoTrace>();
  trace->fail_init = true;
  EXPECT_EQ(OpenMaster(trace).status().code(), absl::StatusCode::kUnavailable);
  EXPECT_EQ(trace->calls, (std::vector<std::string>{"init", "teardown", "destroy"}));
  for (const auto& id : trace->threads) EXPECT_NE(id, std::this_thread::get_id());
}

TEST(EthercatMasterTest, StartupSdoAndEntireLifecycleHaveOneOwner) {
  auto trace = std::make_shared<IoTrace>();
  auto opened = OpenMaster(trace);
  ASSERT_TRUE(opened.ok()) << opened.status();
  auto master = std::move(*opened);
  auto read = master->ReadSdo({1, 0x2000, 0}, 36, 1s);
  ASSERT_TRUE(read.ok());
  EXPECT_EQ(*read, (std::vector<uint8_t>{0x12, 0x34}));
  EXPECT_TRUE(master->WriteSdo({2, 0x2010, 3}, {4, 5, 6}, 1s).ok());
  EXPECT_TRUE(master->Stop().ok());
  EXPECT_TRUE(master->Stop().ok());
  ASSERT_FALSE(trace->threads.empty());
  for (const auto& id : trace->threads) {
    EXPECT_EQ(id, trace->threads.front());
    EXPECT_NE(id, std::this_thread::get_id());
  }
  EXPECT_EQ(trace->sdo_calls, 2);
  EXPECT_GT(trace->sdo_timeout, 0);
  EXPECT_LE(trace->sdo_timeout, 1000000);
  EXPECT_EQ(trace->last_address.slave, 2);
  EXPECT_EQ(trace->last_address.index, 0x2010);
  EXPECT_EQ(trace->last_address.subindex, 3);
  EXPECT_EQ(trace->last_write, (std::vector<uint8_t>{4, 5, 6}));
  EXPECT_EQ(trace->calls.back(), "destroy");
}

TEST(EthercatMasterTest, CyclesWithoutCallerAndRejectsMailboxUntilStopped) {
  auto trace = std::make_shared<IoTrace>();
  auto opened = OpenMaster(trace);
  ASSERT_TRUE(opened.ok());
  auto master = std::move(*opened);
  EXPECT_EQ(master->StartCyclic({{0, 0}}).code(), absl::StatusCode::kInvalidArgument);
  ASSERT_TRUE(master->StartCyclic({{0, 0}, {9, 9}}).ok());
  EXPECT_EQ(master->ReadSdo({1, 0x2000, 0}, 36, 1s).status().code(),
            absl::StatusCode::kFailedPrecondition);
  EXPECT_EQ(master->WriteSdo({1, 0x2010, 0}, {1}, 1s).code(),
            absl::StatusCode::kFailedPrecondition);
  EXPECT_EQ(master->StartCyclic({{0, 0}, {9, 9}}).code(), absl::StatusCode::kFailedPrecondition);
  ASSERT_TRUE(master->SetOutputs(1, {3, 4}).ok());
  ASSERT_TRUE(master->SetOutputs(2, {5, 6}).ok());
  uint64_t sequence = 0;
  bool observed = false;
  for (int i = 0; i < 3; ++i) {
    auto snapshot = master->WaitForCycle(sequence, 1s);
    ASSERT_TRUE(snapshot.ok()) << snapshot.status();
    sequence = snapshot->sequence;
    if (snapshot->data.inputs == std::vector<uint8_t>({3, 4, 5, 6})) {
      observed = true;
      break;
    }
  }
  EXPECT_TRUE(observed);
  auto next = master->WaitForCycle(sequence, 1s);
  ASSERT_TRUE(next.ok());
  EXPECT_GT(next->sequence, sequence);
  EXPECT_EQ(master->SetOutputs(0, {1, 2}).code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(master->SetOutputs(1, {1}).code(), absl::StatusCode::kInvalidArgument);
  EXPECT_TRUE(master->Stop().ok());
  ASSERT_FALSE(trace->sent.empty());
  EXPECT_EQ(trace->sent.back(), (std::vector<uint8_t>{0, 0, 9, 9}));
  EXPECT_EQ(trace->sdo_calls, 0);
  EXPECT_EQ(master->WaitForCycle(0, 1s).status().code(), absl::StatusCode::kCancelled);
  EXPECT_EQ(master->SetOutputs(1, {1, 2}).code(), absl::StatusCode::kCancelled);
  for (const auto& id : trace->threads) EXPECT_EQ(id, trace->threads.front());
}

TEST(EthercatMasterTest, QueuedTimeoutIsNeverDispatched) {
  auto trace = std::make_shared<IoTrace>();
  trace->hold = "sdo";
  auto opened = OpenMaster(trace);
  ASSERT_TRUE(opened.ok());
  auto master = std::move(*opened);
  auto first = std::async(std::launch::async, [&] {
    return master->ReadSdo({1, 0x2000, 0}, 36, 1s);
  });
  ASSERT_TRUE(trace->WaitEntered());
  auto expired = master->WriteSdo({1, 0x2010, 0}, {1}, 10ms);
  trace->Release();
  EXPECT_EQ(expired.code(), absl::StatusCode::kDeadlineExceeded);
  EXPECT_NE(expired.message().find("not executed"), std::string::npos);
  EXPECT_TRUE(first.get().ok());
  EXPECT_TRUE(master->WriteSdo({2, 0x2010, 0}, {2}, 1s).ok());
  EXPECT_TRUE(master->Stop().ok());
  EXPECT_EQ(trace->sdo_calls, 2);
  EXPECT_EQ(trace->last_write, (std::vector<uint8_t>{2}));
}

TEST(EthercatMasterTest, DispatchedTimeoutFaultsMasterAndDiscardsLateSuccess) {
  auto trace = std::make_shared<IoTrace>();
  trace->hold = "sdo";
  auto opened = OpenMaster(trace);
  ASSERT_TRUE(opened.ok());
  auto master = std::move(*opened);
  auto pending = std::async(std::launch::async, [&] {
    return master->ReadSdo({1, 0x2000, 0}, 36, 100ms);
  });
  ASSERT_TRUE(trace->WaitEntered());
  auto expired = pending.get();
  EXPECT_EQ(expired.status().code(), absl::StatusCode::kDeadlineExceeded);
  EXPECT_NE(expired.status().message().find("outcome unknown"), std::string::npos);
  EXPECT_EQ(master->ReadSdo({1, 0x2000, 0}, 36, 1s).status().code(),
            absl::StatusCode::kDeadlineExceeded);
  trace->Release();
  EXPECT_TRUE(master->Stop().ok());
  EXPECT_EQ(trace->sdo_calls, 1);
}

TEST(EthercatMasterTest, StopWakesSdoWaitersBeforeBlockedBackendReturns) {
  auto trace = std::make_shared<IoTrace>();
  trace->hold = "sdo";
  auto opened = OpenMaster(trace);
  ASSERT_TRUE(opened.ok());
  auto master = std::move(*opened);
  auto active = std::async(std::launch::async, [&] {
    return master->ReadSdo({1, 0x2000, 0}, 36, 1s);
  });
  ASSERT_TRUE(trace->WaitEntered());
  auto queued = std::async(std::launch::async, [&] {
    return master->WriteSdo({1, 0x2010, 0}, {1}, 1s);
  });
  auto stopping = std::async(std::launch::async, [&] { return master->Stop(); });
  const auto active_ready = active.wait_for(500ms);
  const auto queued_ready = queued.wait_for(500ms);
  trace->Release();
  EXPECT_EQ(active_ready, std::future_status::ready);
  EXPECT_EQ(queued_ready, std::future_status::ready);
  EXPECT_EQ(active.get().status().code(), absl::StatusCode::kCancelled);
  EXPECT_EQ(queued.get().code(), absl::StatusCode::kCancelled);
  EXPECT_TRUE(stopping.get().ok());
  EXPECT_EQ(trace->sdo_calls, 1);
}

TEST(EthercatMasterTest, BusAndStateFailuresWakeCycleWaiters) {
  for (bool working_count_failure : {true, false}) {
    auto trace = std::make_shared<IoTrace>();
    auto opened = OpenMaster(trace);
    ASSERT_TRUE(opened.ok());
    auto master = std::move(*opened);
    ASSERT_TRUE(master->StartCyclic({{0, 0}, {0, 0}}).ok());
    auto first = master->WaitForCycle(0, 1s);
    ASSERT_TRUE(first.ok());
    if (working_count_failure)
      trace->bad_wkc = true;
    else
      trace->not_operational = true;
    EXPECT_EQ(master->WaitForCycle(UINT64_MAX, 1s).status().code(), absl::StatusCode::kUnavailable);
    (void)master->Stop();
    EXPECT_EQ(trace->sent.back(), (std::vector<uint8_t>{0, 0, 0, 0}));
    EXPECT_EQ(trace->calls.back(), "destroy");
  }
}

TEST(EthercatMasterTest, MissedCyclicDeadlineFailsRatherThanPublishingStaleSnapshot) {
  auto trace = std::make_shared<IoTrace>();
  trace->hold = "exchange";
  auto opened = OpenMaster(trace);
  ASSERT_TRUE(opened.ok());
  auto master = std::move(*opened);
  ASSERT_TRUE(master->StartCyclic({{0, 0}, {0, 0}}).ok());
  ASSERT_TRUE(trace->WaitEntered());
  std::this_thread::sleep_for(75ms);
  trace->Release();
  EXPECT_EQ(master->WaitForCycle(0, 1s).status().code(), absl::StatusCode::kDeadlineExceeded);
  EXPECT_TRUE(master->Stop().ok());
}

TEST(EthercatMasterTest, SdoQueuedDuringStartupCannotBypassCyclicGate) {
  auto trace = std::make_shared<IoTrace>();
  trace->hold = "start";
  auto opened = OpenMaster(trace);
  ASSERT_TRUE(opened.ok());
  auto master = std::move(*opened);
  auto start = std::async(std::launch::async, [&] {
    return master->StartCyclic({{0, 0}, {0, 0}});
  });
  ASSERT_TRUE(trace->WaitEntered());
  auto queued = std::async(std::launch::async, [&] {
    return master->ReadSdo({1, 0x2000, 0}, 36, 1s);
  });
  trace->Release();
  EXPECT_TRUE(start.get().ok());
  EXPECT_EQ(queued.get().status().code(), absl::StatusCode::kFailedPrecondition);
  EXPECT_TRUE(master->WaitForCycle(0, 1s).ok());
  EXPECT_TRUE(master->Stop().ok());
  EXPECT_EQ(trace->sdo_calls, 0);
}

TEST(EthercatMasterTest, StopWakesCycleWaitersAndWaitTimeoutDoesNotStopMaster) {
  auto trace = std::make_shared<IoTrace>();
  auto opened = OpenMaster(trace);
  ASSERT_TRUE(opened.ok());
  auto master = std::move(*opened);
  EXPECT_EQ(master->WaitForCycle(0, 1s).status().code(), absl::StatusCode::kFailedPrecondition);
  EXPECT_EQ(master->ReadSdo({0, 0x2000, 0}, 36, 1s).status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(master->ReadSdo({1, 0x2000, 0}, 0, 1s).status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(master->WriteSdo({1, 0x2010, 0}, {1}, 0us).code(), absl::StatusCode::kInvalidArgument);
  ASSERT_TRUE(master->StartCyclic({{0, 0}, {0, 0}}).ok());
  EXPECT_EQ(master->WaitForCycle(UINT64_MAX, 1ms).status().code(),
            absl::StatusCode::kDeadlineExceeded);
  EXPECT_TRUE(master->WaitForCycle(0, 1s).ok());
  auto waiter =
      std::async(std::launch::async, [&] { return master->WaitForCycle(UINT64_MAX, 1s); });
  EXPECT_TRUE(master->Stop().ok());
  EXPECT_EQ(waiter.get().status().code(), absl::StatusCode::kCancelled);
}

using SdoBytes = CoeSdoTransfer::Bytes;
constexpr CoeSdoTransfer::Mailbox kMailbox{0x1000, 0x1080, 128, 128};

SdoBytes SdoReply(uint8_t command,
                  const SdoBytes& payload = {},
                  uint8_t counter = 1,
                  uint16_t index = 0x2000) {
  SdoBytes result(128, 0);
  result[0] = command == 0x41 ? static_cast<uint8_t>(10 + payload.size()) : 10;
  result[5] = 3 | (counter << 4);
  result[7] = 0x30;
  result[8] = command;
  result[9] = index & 0xff;
  result[10] = index >> 8;
  if (command == 0x41) result[12] = static_cast<uint8_t>(payload.size());
  std::copy(payload.begin(), payload.end(), result.begin() + (command == 0x41 ? 16 : 12));
  return result;
}

// Register-level fake exercises the actual incremental CoE codec, not a second
// implementation of it. One call equals one EtherCAT datagram in production.
class TestMailboxRegisters : public CoeSdoTransfer::RegisterIo {
 public:
  std::deque<SdoBytes> incoming;
  std::deque<SdoBytes> after_write;
  std::vector<SdoBytes> writes;
  int operations = 0;
  bool write_full = false;
  absl::Status io_status;
  absl::Status Read(uint16_t offset, SdoBytes& bytes, int budget) override {
    ++operations;
    EXPECT_GT(budget, 0);
    if (!io_status.ok()) return io_status;
    if (offset == 0x0805)
      bytes[0] = write_full ? 8 : 0;
    else if (offset == 0x080d)
      bytes[0] = incoming.empty() ? 0 : 8;
    else {
      EXPECT_EQ(offset, kMailbox.read_offset);
      if (incoming.empty()) return absl::UnavailableError("no mailbox reply");
      EXPECT_EQ(bytes.size(), incoming.front().size());
      bytes = incoming.front();
      incoming.pop_front();
    }
    return absl::OkStatus();
  }
  absl::Status Write(uint16_t offset, const SdoBytes& bytes, int budget) override {
    ++operations;
    EXPECT_GT(budget, 0);
    EXPECT_EQ(offset, kMailbox.write_offset);
    writes.push_back(bytes);
    if (!io_status.ok()) return io_status;
    incoming.insert(incoming.end(), after_write.begin(), after_write.end());
    after_write.clear();
    return absl::OkStatus();
  }
};

auto CompleteSdo(CoeSdoTransfer& transfer, TestMailboxRegisters& io) {
  absl::StatusOr<std::optional<SdoBytes>> result;
  for (int i = 0; i < 32; ++i) {
    const auto before = io.operations;
    result = transfer.Step(io, 500);
    EXPECT_LE(io.operations - before, 1);
    if (!result.ok() || result->has_value()) return result;
  }
  ADD_FAILURE() << "test transfer did not complete";
  return result;
}

TEST(CoeSdoTransferTest, ReadGoldenRequestAndExpeditedReply) {
  CoeSdoTransfer transfer;
  TestMailboxRegisters io;
  io.after_write.push_back(SdoReply(0x4b, {0x12, 0x34}));
  ASSERT_TRUE(transfer.Begin(kMailbox, 0x2000, 0, 1, false, {}, 2).ok());
  auto reply = CompleteSdo(transfer, io);
  ASSERT_TRUE(reply.ok()) << reply.status();
  ASSERT_TRUE(reply->has_value());
  EXPECT_EQ(**reply, (SdoBytes{0x12, 0x34}));
  ASSERT_EQ(io.writes.size(), 1);
  EXPECT_EQ(SdoBytes(io.writes[0].begin(), io.writes[0].begin() + 16),
            (SdoBytes{10, 0, 0, 0, 0, 0x13, 0, 0x20, 0x40, 0, 0x20, 0, 0, 0, 0, 0}));
  EXPECT_EQ(transfer.Step(io, 500).status().code(), absl::StatusCode::kFailedPrecondition);
}

TEST(CoeSdoTransferTest, NormalUploadAndDownloadCoverFullJwEnvelope) {
  const SdoBytes payload(76, 0xa5);
  for (bool write : {false, true}) {
    CoeSdoTransfer transfer;
    TestMailboxRegisters io;
    io.after_write.push_back(SdoReply(write ? 0x60 : 0x41, write ? SdoBytes{} : payload));
    ASSERT_TRUE(
        transfer.Begin(kMailbox, 0x2000, 0, 1, write, write ? payload : SdoBytes{}, write ? 0 : 76)
            .ok());
    auto reply = CompleteSdo(transfer, io);
    ASSERT_TRUE(reply.ok()) << reply.status();
    ASSERT_TRUE(reply->has_value());
    EXPECT_EQ(**reply, write ? SdoBytes{} : payload);
    ASSERT_EQ(io.writes.size(), 1);
    if (write) {
      EXPECT_EQ(io.writes[0][0], 86);
      EXPECT_EQ(io.writes[0][8], 0x21);
      EXPECT_EQ(io.writes[0][12], 76);
      EXPECT_EQ(SdoBytes(io.writes[0].begin() + 16, io.writes[0].begin() + 92), payload);
      EXPECT_TRUE(std::all_of(
          io.writes[0].begin() + 92, io.writes[0].end(), [](uint8_t value) { return value == 0; }));
    }
  }
}

TEST(CoeSdoTransferTest, ExpeditedWriteSizesAreExactAndNeverRetried) {
  for (size_t size = 1; size <= 4; ++size) {
    CoeSdoTransfer transfer;
    TestMailboxRegisters io;
    io.after_write.push_back(SdoReply(0x60));
    ASSERT_TRUE(transfer.Begin(kMailbox, 0x2000, 0, 1, true, SdoBytes(size, 0x7a), 0).ok());
    ASSERT_TRUE(CompleteSdo(transfer, io).ok());
    ASSERT_EQ(io.writes.size(), 1);
    EXPECT_EQ(io.writes[0][8], 0x23 | ((4 - size) << 2));
    EXPECT_EQ(SdoBytes(io.writes[0].begin() + 12, io.writes[0].begin() + 12 + size),
              SdoBytes(size, 0x7a));
  }
}

TEST(CoeSdoTransferTest, DrainsRetainedMailboxBeforeSendingWithIndependentReplyCounter) {
  CoeSdoTransfer transfer;
  TestMailboxRegisters io;
  io.incoming.push_back(SdoReply(0x43, {9, 9, 9, 9}));
  io.after_write.push_back(SdoReply(0x43, {1, 2, 3, 4}, 7));
  ASSERT_TRUE(transfer.Begin(kMailbox, 0x2000, 0, 1, false, {}, 4).ok());
  auto reply = CompleteSdo(transfer, io);
  ASSERT_TRUE(reply.ok());
  ASSERT_TRUE(reply->has_value());
  EXPECT_EQ(**reply, (SdoBytes{1, 2, 3, 4}));
  EXPECT_EQ(io.writes.size(), 1);
}

TEST(CoeSdoTransferTest, SenderCountersAreIndependentForUploadsAndDownloads) {
  CoeSdoTransfer transfer;
  // Reuse the codec across transfers: neither equal counters, sequential slave
  // counters nor synchronized wraparound are required. Zero is also valid on
  // receive. Keep the master's own outgoing 1..7 counter unchanged on the wire.
  for (uint8_t request_counter = 1; request_counter <= 7; ++request_counter) {
    for (uint8_t response_counter : {7, 1, 5, 0, 3, 6, 2, 4}) {
      for (uint8_t command : {0x43, 0x41, 0x60}) {
        SCOPED_TRACE(::testing::Message()
                     << unsigned(request_counter) << '/' << unsigned(response_counter) << '/'
                     << unsigned(command));
        TestMailboxRegisters io;
        const bool write = command == 0x60;
        const SdoBytes payload(command == 0x41 ? 36 : 4, 0xa5);
        io.after_write.push_back(SdoReply(command, write ? SdoBytes{} : payload, response_counter));
        ASSERT_TRUE(transfer
                        .Begin(kMailbox,
                               0x2000,
                               0,
                               request_counter,
                               write,
                               write ? payload : SdoBytes{},
                               write ? 0 : payload.size())
                        .ok());
        auto reply = CompleteSdo(transfer, io);
        ASSERT_TRUE(reply.ok()) << reply.status();
        ASSERT_TRUE(reply->has_value());
        EXPECT_EQ(**reply, write ? SdoBytes{} : payload);
        ASSERT_EQ(io.writes.size(), 1);
        EXPECT_EQ(io.writes[0][5], 3 | (request_counter << 4));
      }
    }
  }
}

TEST(CoeSdoTransferTest, BusyMailboxesYieldAndCancellationDoesNotSendAgain) {
  CoeSdoTransfer transfer;
  TestMailboxRegisters io;
  io.write_full = true;
  ASSERT_TRUE(transfer.Begin(kMailbox, 0x2000, 0, 1, false, {}, 36).ok());
  for (int i = 0; i < 10; ++i) {
    auto result = transfer.Step(io, 500);
    ASSERT_TRUE(result.ok());
    EXPECT_FALSE(result->has_value());
    EXPECT_EQ(io.operations, i + 1);
  }
  EXPECT_TRUE(io.writes.empty());
  io.write_full = false;
  for (int i = 0; i < 10; ++i) {
    auto result = transfer.Step(io, 500);
    ASSERT_TRUE(result.ok());
    EXPECT_FALSE(result->has_value());
  }
  EXPECT_EQ(io.writes.size(), 1);
  transfer.Cancel();
  EXPECT_EQ(transfer.Step(io, 500).status().code(), absl::StatusCode::kFailedPrecondition);
  EXPECT_EQ(io.writes.size(), 1);
}

TEST(CoeSdoTransferTest, RejectsOversizedSegmentedAndMalformedResponses) {
  CoeSdoTransfer invalid;
  EXPECT_EQ(invalid.Begin(kMailbox, 0x2000, 0, 0, false, {}, 36).code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(invalid.Begin(kMailbox, 0x2000, 0, 1, false, {}, 77).code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(invalid.Begin({0x1000, 0x1080, 32, 32}, 0x2000, 0, 1, false, {}, 36).code(),
            absl::StatusCode::kInvalidArgument);
  for (int mutation = 0; mutation < 7; ++mutation) {
    CoeSdoTransfer transfer;
    TestMailboxRegisters io;
    auto malformed = SdoReply(0x41, SdoBytes(36, 0), 7);
    switch (mutation) {
      case 0:
        malformed[0] = 255;
        break;  // Outside mailbox.
      case 1:
        malformed[9] ^= 1;
        break;  // Wrong index.
      case 2:
        malformed[11] = 1;
        break;  // Wrong subindex.
      case 3:
        malformed[12] = 76;
        break;  // Segmentation would be required.
      case 4:
        malformed[7] = 0x20;
        break;  // Wrong CoE service.
      case 5:
        malformed[8] = 0x60;
        break;  // Wrong SDO command.
      case 6:
        malformed[5] = 0x74;
        break;  // Wrong mailbox type.
    }
    io.after_write.push_back(std::move(malformed));
    ASSERT_TRUE(transfer.Begin(kMailbox, 0x2000, 0, 1, false, {}, 36).ok());
    EXPECT_FALSE(CompleteSdo(transfer, io).ok());
    EXPECT_EQ(io.writes.size(), 1);
  }
}

TEST(CoeSdoTransferTest, ReportsAbortCodeAndDatagramFailureWithoutRetry) {
  CoeSdoTransfer transfer;
  TestMailboxRegisters io;
  io.after_write.push_back(SdoReply(0x80, {0, 0, 2, 6}, 5));
  ASSERT_TRUE(transfer.Begin(kMailbox, 0x2000, 0, 1, false, {}, 36).ok());
  auto aborted = CompleteSdo(transfer, io);
  EXPECT_EQ(aborted.status().code(), absl::StatusCode::kFailedPrecondition);
  EXPECT_NE(aborted.status().message().find("0x06020000"), std::string::npos);
  EXPECT_EQ(io.writes.size(), 1);
  ASSERT_TRUE(transfer.Begin(kMailbox, 0x2000, 0, 2, true, {1}, 0).ok());
  ASSERT_TRUE(transfer.Step(io, 500).ok());  // Drain status.
  ASSERT_TRUE(transfer.Step(io, 500).ok());  // Write status.
  io.io_status = absl::DeadlineExceededError("lost datagram");
  EXPECT_EQ(transfer.Step(io, 500).status().code(), absl::StatusCode::kDeadlineExceeded);
  EXPECT_EQ(io.writes.size(), 2);
  EXPECT_EQ(transfer.Step(io, 500).status().code(), absl::StatusCode::kFailedPrecondition);
  EXPECT_EQ(io.writes.size(), 2);
}

// Runs the real CoE state machine under the real owner scheduler. Only the
// register exchanges are simulated; cyclic tests do not sleep inside mailbox I/O.
struct RuntimeTrace : IoTrace {
  bool withhold_response = false;
  bool overrun_step = false;
  absl::Status step_status;
  int steps = 0;
  int cancel_calls = 0;
  std::vector<int> budgets;
};
class TestRuntimeIo : public TestMasterIo {
 public:
  explicit TestRuntimeIo(std::shared_ptr<RuntimeTrace> trace)
      : TestMasterIo(trace), trace_(std::move(trace)) {}
  bool HasIncrementalSdo() const override {
    trace_->Call("capability");
    return true;
  }
  absl::Status BeginSdo(SdoAddress address, bool write, SdoBytes bytes, size_t capacity) override {
    trace_->Call("sdo_begin");
    if (!trace_->withhold_response)
      registers_.after_write.push_back(SdoReply(
          write ? 0x60 : 0x41, write ? SdoBytes{} : SdoBytes(capacity, 0x5a), 5, address.index));
    return transfer_.Begin(
        kMailbox, address.index, address.subindex, 1, write, std::move(bytes), capacity);
  }
  absl::StatusOr<std::optional<SdoBytes>> StepSdo(int budget) override {
    trace_->Call("sdo_step");
    ++trace_->steps;
    trace_->budgets.push_back(budget);
    if (trace_->overrun_step) std::this_thread::sleep_for(5ms);
    if (!trace_->step_status.ok()) return trace_->step_status;
    return transfer_.Step(registers_, budget);
  }
  void CancelSdo() override {
    trace_->Call("sdo_cancel");
    ++trace_->cancel_calls;
    transfer_.Cancel();
  }

 private:
  std::shared_ptr<RuntimeTrace> trace_;
  TestMailboxRegisters registers_;
  CoeSdoTransfer transfer_;
};

EthercatMaster::Options RuntimeOptions() {
  return {20ms, 1ms, 1ms, 1s, 2ms, 1ms};
}
auto OpenRuntimeMaster(const std::shared_ptr<RuntimeTrace>& trace) {
  return EthercatMaster::Open(std::make_unique<TestRuntimeIo>(trace),
                              "test-only",
                              ProcessDataMode::kSplitLrdLwr,
                              RuntimeOptions());
}

TEST(EthercatMasterTest, RuntimeConfigurationRejectsMissingSupportOrSlack) {
  auto trace = std::make_shared<IoTrace>();
  EXPECT_EQ(OpenMaster(trace, RuntimeOptions()).status().code(),
            absl::StatusCode::kFailedPrecondition);
  auto options = RuntimeOptions();
  options.period = options.process_timeout + options.mailbox_step_budget + options.scheduling_guard;
  EXPECT_EQ(OpenMaster(trace, options).status().code(), absl::StatusCode::kInvalidArgument);
  options = RuntimeOptions();
  options.scheduling_guard = 0us;
  EXPECT_EQ(OpenMaster(trace, options).status().code(), absl::StatusCode::kInvalidArgument);
  options.mailbox_step_budget = 0us;
  options.scheduling_guard = EthercatMaster::Microseconds::max();
  EXPECT_EQ(OpenMaster(trace, options).status().code(), absl::StatusCode::kInvalidArgument);
}

TEST(EthercatMasterTest, RuntimeSdoYieldsBetweenCyclesAndSharesSingleOwner) {
  auto trace = std::make_shared<RuntimeTrace>();
  auto opened = OpenRuntimeMaster(trace);
  ASSERT_TRUE(opened.ok()) << opened.status();
  auto master = std::move(*opened);
  ASSERT_TRUE(master->StartCyclic({{0, 0}, {0, 0}}).ok());
  auto read = master->ReadSdo({1, 0x2000, 0}, 36, 1s);
  ASSERT_TRUE(read.ok()) << read.status();
  EXPECT_EQ(*read, SdoBytes(36, 0x5a));
  EXPECT_TRUE(master->WriteSdo({1, 0x2010, 0}, SdoBytes(76, 0x6b), 1s).ok());
  EXPECT_TRUE(master->Stop().ok());
  EXPECT_GE(trace->steps, 10);
  EXPECT_EQ(trace->sdo_calls, 0);  // No blocking SOEM-style SDO after startup.
  EXPECT_GE(trace->sent.size(), 10);
  for (const auto& id : trace->threads) EXPECT_EQ(id, trace->threads.front());
  for (int budget : trace->budgets) {
    EXPECT_GT(budget, 0);
    EXPECT_LE(budget, 2000);
  }
  bool exchanged = false;
  bool management_step = false;
  for (const auto& call : trace->calls) {
    if (call == "exchange") {
      exchanged = true;
      management_step = false;
    }
    if (call == "sdo_step" || call == "state") {
      EXPECT_TRUE(exchanged);
      EXPECT_FALSE(management_step) << "more than one mailbox/state step per cycle";
      management_step = true;
    }
  }
}

TEST(EthercatMasterTest, StalledRuntimeMailboxKeepsCyclingUntilDeadlineThenFailsClosed) {
  auto trace = std::make_shared<RuntimeTrace>();
  trace->withhold_response = true;
  auto opened = OpenRuntimeMaster(trace);
  ASSERT_TRUE(opened.ok());
  auto master = std::move(*opened);
  ASSERT_TRUE(master->StartCyclic({{0, 0}, {0, 0}}).ok());
  auto pending = std::async(std::launch::async, [&] {
    return master->ReadSdo({1, 0x2000, 0}, 36, 400ms);
  });
  uint64_t sequence = 0;
  for (int i = 0; i < 8; ++i) {
    auto cycle = master->WaitForCycle(sequence, 100ms);
    ASSERT_TRUE(cycle.ok()) << cycle.status();
    sequence = cycle->sequence;
  }
  EXPECT_EQ(pending.get().status().code(), absl::StatusCode::kDeadlineExceeded);
  EXPECT_EQ(master->ReadSdo({1, 0x2000, 0}, 36, 1s).status().code(),
            absl::StatusCode::kDeadlineExceeded);
  EXPECT_TRUE(master->Stop().ok());
  EXPECT_GE(trace->steps, 5);
  EXPECT_GE(trace->sent.size(), 9);
  EXPECT_GE(trace->cancel_calls, 1);
  EXPECT_EQ(trace->sent.back(), (SdoBytes{0, 0, 0, 0}));
}

TEST(EthercatMasterTest, RuntimeStopCancelsTransferAndQueuedCalls) {
  auto trace = std::make_shared<RuntimeTrace>();
  trace->withhold_response = true;
  trace->hold = "sdo_begin";
  auto opened = OpenRuntimeMaster(trace);
  ASSERT_TRUE(opened.ok());
  auto master = std::move(*opened);
  ASSERT_TRUE(master->StartCyclic({{0, 0}, {0, 0}}).ok());
  auto active = std::async(std::launch::async, [&] {
    return master->ReadSdo({1, 0x2000, 0}, 36, 1s);
  });
  ASSERT_TRUE(trace->WaitEntered());
  auto queued = std::async(std::launch::async, [&] {
    return master->WriteSdo({1, 0x2010, 0}, {1}, 1s);
  });
  auto stopping = std::async(std::launch::async, [&] { return master->Stop(); });
  const auto woke = active.wait_for(200ms);
  trace->Release();
  EXPECT_EQ(woke, std::future_status::ready);
  EXPECT_EQ(active.get().status().code(), absl::StatusCode::kCancelled);
  EXPECT_EQ(queued.get().code(), absl::StatusCode::kCancelled);
  EXPECT_TRUE(stopping.get().ok());
  EXPECT_EQ(trace->cancel_calls, 1);
  EXPECT_EQ(trace->steps, 0);
  EXPECT_EQ(trace->calls.back(), "destroy");
}

TEST(EthercatMasterTest, RuntimeBackendOverrunFailsWithoutAnotherMailboxStep) {
  auto trace = std::make_shared<RuntimeTrace>();
  trace->overrun_step = true;
  auto opened = OpenRuntimeMaster(trace);
  ASSERT_TRUE(opened.ok());
  auto master = std::move(*opened);
  ASSERT_TRUE(master->StartCyclic({{0, 0}, {0, 0}}).ok());
  EXPECT_EQ(master->ReadSdo({1, 0x2000, 0}, 36, 1s).status().code(),
            absl::StatusCode::kDeadlineExceeded);
  EXPECT_TRUE(master->Stop().ok());
  EXPECT_EQ(trace->steps, 1);
}

TEST(EthercatMasterTest, RuntimeBackendOverrunPreservesRegisterDiagnostic) {
  auto trace = std::make_shared<RuntimeTrace>();
  trace->overrun_step = true;
  trace->step_status = absl::DeadlineExceededError(
      "EtherCAT register datagram deadline; register=0x80d budget_us=2000 wkc=-1");
  auto opened = OpenRuntimeMaster(trace);
  ASSERT_TRUE(opened.ok());
  auto master = std::move(*opened);
  ASSERT_TRUE(master->StartCyclic({{0, 0}, {0, 0}}).ok());
  const auto result = master->ReadSdo({1, 0x2000, 0}, 36, 1s);
  EXPECT_EQ(result.status().code(), absl::StatusCode::kDeadlineExceeded);
  EXPECT_NE(result.status().message().find("backend exceeded step budget"), std::string::npos);
  EXPECT_NE(result.status().message().find("register=0x80d budget_us=2000 wkc=-1"),
            std::string::npos);
  EXPECT_TRUE(master->Stop().ok());
  EXPECT_EQ(trace->steps, 1);
  EXPECT_EQ(trace->sent.back(), (SdoBytes{0, 0, 0, 0}));
}

}  // namespace
}  // namespace robot::comm::ethercat
