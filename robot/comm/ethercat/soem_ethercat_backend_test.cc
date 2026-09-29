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
#include "firmware/am243/joshua_dual_transport_v1/src/joshua_ethercat_profile.h"
#include "firmware/common/joshua_wire_ethercat.h"
#include "gtest/gtest.h"
#include "robot/board/factory/board_factory.h"
#include "robot/comm/ethercat/coe_sdo_transfer.h"
#include "robot/comm/ethercat/ethercat_master.h"
#include "robot/comm/ethercat/ethercat_types.h"
#include "robot/comm/ethercat/joshua_wire_ethercat_transport.h"
#include "robot/comm/factory/comm_factory.h"

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
  auto first =
      std::async(std::launch::async, [&] { return master->ReadSdo({1, 0x2000, 0}, 36, 1s); });
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
  auto pending =
      std::async(std::launch::async, [&] { return master->ReadSdo({1, 0x2000, 0}, 36, 100ms); });
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
  auto active =
      std::async(std::launch::async, [&] { return master->ReadSdo({1, 0x2000, 0}, 36, 1s); });
  ASSERT_TRUE(trace->WaitEntered());
  auto queued =
      std::async(std::launch::async, [&] { return master->WriteSdo({1, 0x2010, 0}, {1}, 1s); });
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
  auto start =
      std::async(std::launch::async, [&] { return master->StartCyclic({{0, 0}, {0, 0}}); });
  ASSERT_TRUE(trace->WaitEntered());
  auto queued =
      std::async(std::launch::async, [&] { return master->ReadSdo({1, 0x2000, 0}, 36, 1s); });
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

TEST(CoeSdoTransferTest, NormalUploadAndDownloadCoverFullJw2Envelope) {
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

TEST(CoeSdoTransferTest, DrainsRetainedMailboxAndIgnoresWrongCounter) {
  CoeSdoTransfer transfer;
  TestMailboxRegisters io;
  io.incoming.push_back(SdoReply(0x43, {9, 9, 9, 9}));
  io.after_write.push_back(SdoReply(0x43, {8, 8, 8, 8}, 7));
  io.after_write.push_back(SdoReply(0x43, {1, 2, 3, 4}));
  ASSERT_TRUE(transfer.Begin(kMailbox, 0x2000, 0, 1, false, {}, 4).ok());
  auto reply = CompleteSdo(transfer, io);
  ASSERT_TRUE(reply.ok());
  ASSERT_TRUE(reply->has_value());
  EXPECT_EQ(**reply, (SdoBytes{1, 2, 3, 4}));
  EXPECT_EQ(io.writes.size(), 1);
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
    auto malformed = SdoReply(0x41, SdoBytes(36, 0));
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
        malformed[5] = 0x14;
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
  io.after_write.push_back(SdoReply(0x80, {0, 0, 2, 6}));
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
          write ? 0x60 : 0x41, write ? SdoBytes{} : SdoBytes(capacity, 0x5a), 1, address.index));
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
  auto pending =
      std::async(std::launch::async, [&] { return master->ReadSdo({1, 0x2000, 0}, 36, 400ms); });
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
  auto active =
      std::async(std::launch::async, [&] { return master->ReadSdo({1, 0x2000, 0}, 36, 1s); });
  ASSERT_TRUE(trace->WaitEntered());
  auto queued =
      std::async(std::launch::async, [&] { return master->WriteSdo({1, 0x2010, 0}, {1}, 1s); });
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

// Profile/adapter regressions use the same owner-worker test seam. This fake
// supplies object-dictionary/PDO bytes only; it never opens a socket or device.
uint32_t JwecU32(const uint8_t* p) {
  return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
void JwecPut32(uint8_t* p, uint32_t value) {
  for (int i = 0; i < 4; ++i) p[i] = value >> (8 * i);
}
SdoBytes JwecDescriptor() {
  return {'J', 'W', 'E', 'C', 1,   0,   2,   0,   2,   0,   1,   0,   80, 0, 80, 0, 64, 0,
          6,   0,   0,   0,   't', 'e', 's', 't', '-', 'j', 'w', '2', 0,  0, 0,  0, 0,  0};
}
SdoBytes JwecRequest(uint32_t id,
                     uint8_t command,
                     uint32_t session = 7,
                     uint8_t channel = JW_CHANNEL_NONE) {
  SdoBytes frame(64);
  frame.resize(
      jw2_encode_frame(frame.data(), frame.size(), session, id, command, channel, nullptr, 0));
  return frame;
}
SdoBytes JwecReply(const SdoBytes& request, int mutation = 0) {
  jw2_frame_t frame;
  EXPECT_EQ(jw2_decode_frame(request.data(), request.size(), &frame), 0);
  if (mutation == 1) ++frame.message_id;
  if (mutation == 2) frame.cmd = JW_CMD_ENABLE;
  if (mutation == 3) frame.channel = 2;
  SdoBytes response(64);
  const uint8_t ok = JW_STATUS_OK;
  response.resize(jw2_encode_response(response.data(), response.size(), &frame, &ok, 1));
  if (mutation == 4) response.back() ^= 1;
  return response;
}

struct JwecTrace : IoTrace {
  JwecTrace() {
    outputs.assign(160, 0);
  }
  SdoBytes descriptor = JwecDescriptor();
  uint32_t sessions[2] = {};
  uint32_t accepted[2] = {};
  SdoBytes mailbox[2] = {SdoBytes(76), SdoBytes(76)};
  SdoBytes inputs[2] = {SdoBytes(80), SdoBytes(80)};
  SdoBytes held[2];
  uint32_t held_generation[2] = {};
  std::vector<std::pair<SdoAddress, SdoBytes>> writes;
  std::vector<SdoBytes> pdo_requests;
  std::atomic<bool> hold_pdo{false};
  std::atomic<bool> hold_acceptance{false};
  std::atomic<bool> hold_mailbox{false};
  std::atomic<bool> stale_reset{false};
  std::atomic<bool> reboot{false};
  std::atomic<bool> inject_stale_pdo{false};
  std::atomic<bool> inject_stale_mailbox{false};
  std::atomic<bool> inject_old_session_mailbox{false};
  std::atomic<int> mutation{0};
  std::atomic<int> seen_pdo{0};
};

class JwecIo : public TestMasterIo {
 public:
  explicit JwecIo(std::shared_ptr<JwecTrace> trace)
      : TestMasterIo(trace), trace_(std::move(trace)) {}
  bool HasIncrementalSdo() const override {
    return true;
  }
  absl::StatusOr<PdoRegion> GetPdoRegion(uint16_t slave) const override {
    trace_->Call("region");
    return PdoRegion{slave, size_t((slave - 1) * 80), size_t((slave - 1) * 80), 80, 80};
  }
  absl::StatusOr<SdoBytes> ReadSdo(SdoAddress a, size_t capacity, int budget) override {
    trace_->Call("sdo_read");
    EXPECT_GT(budget, 0);
    auto result = Object(a, false, {});
    if (result.ok()) {
      EXPECT_LE(result->size(), capacity);
    }
    return result;
  }
  absl::Status WriteSdo(SdoAddress a, const SdoBytes& bytes, int budget) override {
    trace_->Call("sdo_write");
    EXPECT_GT(budget, 0);
    return Object(a, true, bytes).status();
  }
  absl::Status BeginSdo(SdoAddress address, bool write, SdoBytes bytes, size_t) override {
    trace_->Call("sdo_begin");
    address_ = address;
    write_ = write;
    bytes_ = std::move(bytes);
    return absl::OkStatus();
  }
  absl::StatusOr<std::optional<SdoBytes>> StepSdo(int budget) override {
    trace_->Call("sdo_step");
    EXPECT_GT(budget, 0);
    auto result = Object(address_, write_, bytes_);
    if (!result.ok()) return result.status();
    return std::optional<SdoBytes>(std::move(*result));
  }
  void CancelSdo() override {
    trace_->Call("sdo_cancel");
  }
  absl::StatusOr<ProcessData> ExchangeProcessData(int budget) override {
    trace_->Call("exchange");
    EXPECT_GT(budget, 0);
    std::lock_guard lock(trace_->mutex);
    trace_->sent.push_back(trace_->outputs);
    SdoBytes input;
    for (size_t i = 0; i < 2; ++i) {
      if (i == 0 && trace_->reboot.exchange(false)) {
        trace_->sessions[i] = 0;
        trace_->accepted[i] = 0;
        trace_->inputs[i].assign(80, 0);
        trace_->held[i].clear();
      }
      const auto* output = trace_->outputs.data() + i * 80;
      auto& response = trace_->inputs[i];
      const uint32_t generation = JwecU32(output + 4);
      const uint32_t ack = JwecU32(output + 8);
      if (ack != 0 && ack == JwecU32(response.data() + 8)) {
        std::fill(response.begin() + 8, response.end(), 0);
      }
      if (generation != 0 && JwecU32(output) == trace_->sessions[i]) {
        ++trace_->seen_pdo;
        if (generation > trace_->accepted[i] && !trace_->hold_acceptance) {
          trace_->accepted[i] = generation;
          const size_t length = output[12] | (size_t(output[13]) << 8);
          EXPECT_LE(length, 64);
          trace_->held[i] = SdoBytes(output + 16, output + 16 + std::min(length, size_t(64)));
          trace_->pdo_requests.push_back(trace_->held[i]);
          trace_->held_generation[i] = generation;
        }
      }
      JwecPut32(response.data(), trace_->sessions[i]);
      JwecPut32(response.data() + 4, trace_->accepted[i]);
      if (!trace_->held[i].empty() && !trace_->hold_pdo) {
        const auto frame = JwecReply(trace_->held[i], trace_->mutation);
        JwecPut32(response.data() + 8,
                  trace_->held_generation[i] + (trace_->mutation == 5 ? 1 : 0));
        response[12] = frame.size();
        response[14] = trace_->mutation == 6 ? 1 : 0;
        std::copy(frame.begin(), frame.end(), response.begin() + 16);
        trace_->held[i].clear();
      }
      auto published = response;
      if (generation != 0 && trace_->inject_stale_pdo.exchange(false))
        JwecPut32(published.data(), 99);
      input.insert(input.end(), published.begin(), published.end());
    }
    return ProcessData{trace_->outputs, input, 6, trace_->bad_wkc ? 0 : 6};
  }

 private:
  absl::StatusOr<SdoBytes> Object(SdoAddress address, bool write, const SdoBytes& bytes) {
    std::lock_guard lock(trace_->mutex);
    const size_t i = address.slave - 1;
    EXPECT_EQ(address.subindex, 0);
    if (write) trace_->writes.emplace_back(address, bytes);
    if (address.index == JWEC_DESCRIPTOR_INDEX && !write) return trace_->descriptor;
    if (address.index == JWEC_SESSION_INDEX) {
      if (write) {
        EXPECT_EQ(bytes.size(), 8);
        EXPECT_EQ(JwecU32(bytes.data()), 1);
        trace_->sessions[i] = JwecU32(bytes.data() + 4);
        trace_->accepted[i] = 0;
        trace_->mailbox[i].assign(76, 0);
        trace_->inputs[i].assign(80, 0);
        trace_->held[i].clear();
        return SdoBytes{};
      }
      SdoBytes result(8);
      JwecPut32(result.data(), 1);
      JwecPut32(result.data() + 4, trace_->stale_reset ? 99 : trace_->sessions[i]);
      return result;
    }
    if (address.index == JWEC_REQUEST_INDEX && write) {
      EXPECT_EQ(bytes.size(), 76);
      const size_t size = bytes[8] | (size_t(bytes[9]) << 8);
      auto frame =
          JwecReply(SdoBytes(bytes.begin() + 12, bytes.begin() + 12 + size), trace_->mutation);
      auto& response = trace_->mailbox[i];
      response.assign(76, 0);
      std::copy_n(bytes.begin(), 8, response.begin());
      response[8] = frame.size();
      std::copy(frame.begin(), frame.end(), response.begin() + 12);
      return SdoBytes{};
    }
    if (address.index == JWEC_RESPONSE_INDEX && !write) {
      if (trace_->hold_mailbox) return SdoBytes(76);
      auto response = trace_->mailbox[i];
      if (trace_->inject_old_session_mailbox.exchange(false)) JwecPut32(response.data(), 99);
      if (trace_->inject_stale_mailbox.exchange(false)) {
        const uint32_t generation = JwecU32(response.data() + 4);
        EXPECT_GT(generation, 1);
        JwecPut32(response.data() + 4, generation - 1);
      }
      return response;
    }
    if (address.index == JWEC_ACK_INDEX && write) {
      EXPECT_EQ(bytes.size(), 4);
      if (JwecU32(bytes.data()) == JwecU32(trace_->mailbox[i].data() + 4))
        trace_->mailbox[i].assign(76, 0);
      return SdoBytes{};
    }
    return absl::InvalidArgumentError("unexpected fake object");
  }
  std::shared_ptr<JwecTrace> trace_;
  SdoAddress address_{};
  bool write_ = false;
  SdoBytes bytes_;
};

TEST(JwecProfileTest, ExactDescriptorAndActionableMismatchDiagnostics) {
  PdoRegion region{1, 0, 0, 80, 80};
  EXPECT_TRUE(ValidateJoshuaWireEthercatProfile(JwecDescriptor(), region, 6).ok());
  for (size_t offset : {0, 4, 6, 8, 10, 12, 14, 16, 18, 34}) {
    auto d = JwecDescriptor();
    d[offset] = offset == 6 ? 3 : 0;
    if (offset == 34) d[offset] = 1;
    auto status = ValidateJoshuaWireEthercatProfile(d, region, 6);
    EXPECT_EQ(status.code(), absl::StatusCode::kFailedPrecondition) << offset;
    EXPECT_NE(status.message().find("test-jw2"), std::string::npos);
    EXPECT_NE(status.message().find("Build and flash"), std::string::npos);
    EXPECT_NE(status.message().find("PDO 80/80"), std::string::npos);
  }
  for (size_t size : {0, 8, 35, 37})
    EXPECT_FALSE(ValidateJoshuaWireEthercatProfile(SdoBytes(size), region, 6).ok());
  region.input_size_bytes = 8;
  EXPECT_FALSE(ValidateJoshuaWireEthercatProfile(JwecDescriptor(), region, 6).ok());
}

TEST(JwecProfileTest, MismatchNeverResetsOrStartsSlaveAndClaimCannotBeReused) {
  auto trace = std::make_shared<JwecTrace>();
  trace->descriptor[10] = 2;
  auto opened = EthercatMaster::Open(std::make_unique<JwecIo>(trace),
                                     "test-only",
                                     ProcessDataMode::kSplitLrdLwr,
                                     RuntimeOptions());
  ASSERT_TRUE(opened.ok());
  std::shared_ptr<EthercatMaster> master(std::move(*opened));
  EXPECT_EQ(JoshuaWireEthercatTransport::Open(master, 1, {1s, 5ms}).status().code(),
            absl::StatusCode::kFailedPrecondition);
  EXPECT_EQ(JoshuaWireEthercatTransport::Open(master, 1, {1s, 5ms}).status().code(),
            absl::StatusCode::kAlreadyExists);
  EXPECT_TRUE(master->Stop().ok());
  EXPECT_TRUE(trace->writes.empty());
  EXPECT_EQ(std::count(trace->calls.begin(), trace->calls.end(), "start"), 0);
}

TEST(JwecProfileTest, ResetRequiresExactReadbackBeforeAnyCommandCanBePublished) {
  auto trace = std::make_shared<JwecTrace>();
  trace->stale_reset = true;
  auto opened = EthercatMaster::Open(std::make_unique<JwecIo>(trace),
                                     "test-only",
                                     ProcessDataMode::kSplitLrdLwr,
                                     RuntimeOptions());
  ASSERT_TRUE(opened.ok());
  std::shared_ptr<EthercatMaster> master(std::move(*opened));
  auto opened_endpoint = JoshuaWireEthercatTransport::Open(master, 1, {100ms, 5ms});
  ASSERT_TRUE(opened_endpoint.ok());
  auto endpoint = *opened_endpoint;
  EXPECT_EQ(endpoint->Exchange(JwecRequest(1, JW_CMD_RESET_SESSION)).status().code(),
            absl::StatusCode::kDeadlineExceeded);
  EXPECT_EQ(endpoint->Exchange(JwecRequest(2, JW_CMD_ENABLE)).status().code(),
            absl::StatusCode::kFailedPrecondition);
  endpoint->Stop();
  EXPECT_TRUE(master->Stop().ok());
  ASSERT_EQ(trace->writes.size(), 1);
  EXPECT_EQ(trace->writes.front().first.index, JWEC_SESSION_INDEX);
  EXPECT_EQ(trace->writes.front().second, (SdoBytes{1, 0, 0, 0, 7, 0, 0, 0}));
}

class JwecAdapterTest : public ::testing::Test {
 protected:
  void SetUp() override {
    trace = std::make_shared<JwecTrace>();
    auto opened = EthercatMaster::Open(std::make_unique<JwecIo>(trace),
                                       "test-only",
                                       ProcessDataMode::kSplitLrdLwr,
                                       RuntimeOptions());
    ASSERT_TRUE(opened.ok());
    master = std::shared_ptr<EthercatMaster>(std::move(*opened));
    for (int i = 0; i < 2; ++i) {
      auto endpoint = JoshuaWireEthercatTransport::Open(master, i + 1, {1s, 5ms});
      ASSERT_TRUE(endpoint.ok()) << endpoint.status();
      endpoints[i] = *endpoint;
      ASSERT_TRUE(endpoints[i]->Exchange(JwecRequest(1, JW_CMD_RESET_SESSION)).ok());
    }
    ASSERT_TRUE(master
                    ->StartCyclic({JoshuaWireEthercatTransport::StopImage(),
                                   JoshuaWireEthercatTransport::StopImage()})
                    .ok());
  }
  void TearDown() override {
    for (auto& endpoint : endpoints)
      if (endpoint) endpoint->Stop();
    if (master) {
      const auto stopped = master->Stop();
      if (!allow_shutdown_failure) {
        EXPECT_TRUE(stopped.ok()) << stopped;
      }
    }
  }
  void WaitPublished() {
    for (int i = 0; i < 20 && trace->seen_pdo == 0; ++i) {
      auto cycle = master->WaitForCycle(sequence, 100ms);
      ASSERT_TRUE(cycle.ok()) << cycle.status();
      sequence = cycle->sequence;
    }
    ASSERT_GT(trace->seen_pdo, 0);
  }
  std::shared_ptr<JwecTrace> trace;
  std::shared_ptr<EthercatMaster> master;
  std::shared_ptr<JoshuaWireEthercatTransport> endpoints[2];
  uint64_t sequence = 0;
  bool allow_shutdown_failure = false;
};

TEST_F(JwecAdapterTest, RoutingGoldenEnvelopesStaleRepliesAndSingleBusOwner) {
  auto& endpoint = endpoints[0];
  EXPECT_FALSE(endpoint->Exchange(JwecRequest(2, JW_CMD_GET_FEEDBACK)).ok());
  EXPECT_FALSE(endpoint->Exchange(JwecRequest(2, JW_CMD_ENABLE), absl::Seconds(1)).ok());
  EXPECT_EQ(endpoint->Send(JwecRequest(2, JW_CMD_ENABLE)).code(), absl::StatusCode::kUnimplemented);
  EXPECT_FALSE(
      endpoint->Exchange(JwecRequest(2, JW_CMD_GET_FEEDBACK), absl::InfiniteDuration()).ok());
  auto identify = JwecRequest(2, JW_CMD_IDENTIFY);
  trace->inject_old_session_mailbox = true;
  auto response = endpoint->Exchange(identify);
  ASSERT_TRUE(response.ok()) << response.status();
  EXPECT_EQ(*response, JwecReply(identify));
  trace->inject_stale_pdo = true;
  auto feedback = JwecRequest(3, JW_CMD_GET_FEEDBACK, 7, 0);
  response = endpoint->Exchange(feedback, absl::Seconds(1));
  ASSERT_TRUE(response.ok()) << response.status();
  EXPECT_EQ(*response, JwecReply(feedback));
  trace->inject_stale_mailbox = true;
  ASSERT_TRUE(endpoint->Exchange(JwecRequest(4, JW_CMD_DISABLE)).ok());
  EXPECT_FALSE(endpoint->Exchange(JwecRequest(4, JW_CMD_DISABLE)).ok());
  for (auto& e : endpoints) e->Stop();
  ASSERT_TRUE(master->Stop().ok());
  for (const auto& thread : trace->threads) EXPECT_EQ(thread, trace->threads.front());
  auto written = std::find_if(trace->writes.begin(), trace->writes.end(), [](const auto& w) {
    return w.first.index == JWEC_REQUEST_INDEX;
  });
  ASSERT_NE(written, trace->writes.end());
  EXPECT_EQ(SdoBytes(written->second.begin(), written->second.begin() + 12),
            (SdoBytes{7, 0, 0, 0, 1, 0, 0, 0, 15, 0, 0, 0}));
  EXPECT_TRUE(std::equal(identify.begin(), identify.end(), written->second.begin() + 12));
  EXPECT_TRUE(std::all_of(written->second.begin() + 12 + identify.size(),
                          written->second.end(),
                          [](uint8_t b) { return b == 0; }));
  auto sent = std::find_if(trace->sent.begin(), trace->sent.end(), [](const auto& b) {
    return JwecU32(b.data() + 4) == 2;
  });
  ASSERT_NE(sent, trace->sent.end());
  EXPECT_EQ(SdoBytes(sent->begin(), sent->begin() + 16),
            (SdoBytes{7, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 15, 0, 0, 0}));
  EXPECT_TRUE(std::equal(feedback.begin(), feedback.end(), sent->begin() + 16));
}

TEST_F(JwecAdapterTest, WrongTupleCrcGenerationAndStatusNeverCompleteSuccessfully) {
  for (int mutation = 1; mutation <= 6; ++mutation) {
    const uint32_t session = 7 + mutation;
    ASSERT_TRUE(endpoints[0]->Exchange(JwecRequest(1, JW_CMD_RESET_SESSION, session)).ok());
    trace->mutation = mutation;
    auto reply =
        endpoints[0]->Exchange(JwecRequest(2, JW_CMD_GET_FEEDBACK, session, 0), absl::Seconds(1));
    EXPECT_EQ(reply.status().code(), absl::StatusCode::kDataLoss) << mutation;
    EXPECT_EQ(endpoints[0]
                  ->Exchange(JwecRequest(3, JW_CMD_GET_FEEDBACK, session), absl::Seconds(1))
                  .status()
                  .code(),
              absl::StatusCode::kFailedPrecondition);
  }
  trace->mutation = 0;
  EXPECT_TRUE(endpoints[0]->Exchange(JwecRequest(1, JW_CMD_RESET_SESSION, 14)).ok());
  EXPECT_TRUE(
      endpoints[0]->Exchange(JwecRequest(2, JW_CMD_GET_FEEDBACK, 14), absl::Seconds(1)).ok());
}

TEST_F(JwecAdapterTest, TimedOutAcceptedPdoIsInvalidatedAndLateReplyAcknowledgedWhileIdle) {
  trace->hold_pdo = true;
  auto pending = std::async(std::launch::async, [&] {
    return endpoints[0]->Exchange(JwecRequest(2, JW_CMD_SET_TARGET), absl::Milliseconds(150));
  });
  WaitPublished();
  EXPECT_EQ(pending.get().status().code(), absl::StatusCode::kDeadlineExceeded);
  trace->hold_pdo = false;
  bool acknowledged = false;
  for (int i = 0; i < 20; ++i) {
    auto cycle = master->WaitForCycle(sequence, 100ms);
    ASSERT_TRUE(cycle.ok());
    sequence = cycle->sequence;
    if (JwecU32(cycle->data.outputs.data() + 4) == 0 &&
        JwecU32(cycle->data.outputs.data() + 8) == 1) {
      acknowledged = true;
      break;
    }
  }
  EXPECT_TRUE(acknowledged);
  EXPECT_FALSE(endpoints[0]->Exchange(JwecRequest(2, JW_CMD_SET_TARGET), absl::Seconds(1)).ok());
  EXPECT_TRUE(endpoints[0]->Exchange(JwecRequest(3, JW_CMD_GET_FEEDBACK), absl::Seconds(1)).ok());
}

TEST_F(JwecAdapterTest, UnacceptedTimeoutCancelsWithoutLaterExecution) {
  trace->hold_acceptance = true;
  EXPECT_EQ(endpoints[0]
                ->Exchange(JwecRequest(2, JW_CMD_SET_TARGET), absl::Milliseconds(100))
                .status()
                .code(),
            absl::StatusCode::kDeadlineExceeded);
  // Observe cancellation on the bus before letting the simulated slave accept.
  bool cancelled = false;
  for (int i = 0; i < 10; ++i) {
    auto cycle = master->WaitForCycle(sequence, 100ms);
    ASSERT_TRUE(cycle.ok());
    sequence = cycle->sequence;
    if (JwecU32(cycle->data.outputs.data() + 4) == 0) {
      cancelled = true;
      break;
    }
  }
  ASSERT_TRUE(cancelled);
  trace->hold_acceptance = false;
  EXPECT_TRUE(endpoints[0]->Exchange(JwecRequest(3, JW_CMD_GET_FEEDBACK), absl::Seconds(1)).ok());
  endpoints[0]->Stop();
  endpoints[1]->Stop();
  EXPECT_TRUE(master->Stop().ok());
  ASSERT_EQ(trace->pdo_requests.size(), 1);
  EXPECT_EQ(trace->pdo_requests.front(), JwecRequest(3, JW_CMD_GET_FEEDBACK));
}

TEST_F(JwecAdapterTest, RebootClearsReadinessAndMessageIdsCannotWrapWithinSession) {
  trace->hold_pdo = true;
  auto pending = std::async(std::launch::async, [&] {
    return endpoints[0]->Exchange(JwecRequest(2, JW_CMD_GET_FEEDBACK), absl::Seconds(1));
  });
  WaitPublished();
  trace->reboot = true;
  EXPECT_EQ(pending.get().status().code(), absl::StatusCode::kFailedPrecondition);
  EXPECT_FALSE(endpoints[0]->Exchange(JwecRequest(3, JW_CMD_ENABLE)).ok());
  EXPECT_FALSE(endpoints[0]->Exchange(JwecRequest(1, JW_CMD_RESET_SESSION)).ok());
  trace->hold_pdo = false;
  ASSERT_TRUE(endpoints[0]->Exchange(JwecRequest(1, JW_CMD_RESET_SESSION, 8)).ok());
  ASSERT_TRUE(endpoints[0]->Exchange(JwecRequest(UINT32_MAX, JW_CMD_ESTOP, 8)).ok());
  EXPECT_FALSE(endpoints[0]->Exchange(JwecRequest(1, JW_CMD_ENABLE, 8)).ok());
  ASSERT_TRUE(endpoints[0]->Exchange(JwecRequest(1, JW_CMD_RESET_SESSION, 9)).ok());
  EXPECT_TRUE(
      endpoints[0]->Exchange(JwecRequest(2, JW_CMD_GET_FEEDBACK, 9), absl::Seconds(1)).ok());
}

TEST_F(JwecAdapterTest, QueuedTimeoutDoesNotPublishAndEstopPreemptsCyclicWaiter) {
  trace->hold_acceptance = true;
  auto active = std::async(std::launch::async, [&] {
    return endpoints[0]->Exchange(JwecRequest(2, JW_CMD_SET_TARGET), absl::Seconds(1));
  });
  WaitPublished();
  auto expired = endpoints[0]->Exchange(JwecRequest(3, JW_CMD_SET_TARGET), absl::Milliseconds(10));
  EXPECT_EQ(expired.status().code(), absl::StatusCode::kDeadlineExceeded);
  EXPECT_NE(expired.status().message().find("not executed"), std::string::npos);
  EXPECT_TRUE(endpoints[0]->Exchange(JwecRequest(4, JW_CMD_ESTOP)).ok());
  EXPECT_EQ(active.get().status().code(), absl::StatusCode::kCancelled);
  auto cycle = master->WaitForCycle(sequence, 100ms);
  ASSERT_TRUE(cycle.ok());
  EXPECT_EQ(JwecU32(cycle->data.outputs.data() + 4), 0);
  for (auto& e : endpoints) e->Stop();
  ASSERT_TRUE(master->Stop().ok());
  EXPECT_TRUE(trace->pdo_requests.empty());
}

TEST_F(JwecAdapterTest, StopWakesCallsWithoutStoppingOtherSlaveAndLastLeaseOwnsMaster) {
  trace->hold_pdo = true;
  auto active = std::async(std::launch::async, [&] {
    return endpoints[0]->Exchange(JwecRequest(2, JW_CMD_GET_FEEDBACK), absl::Seconds(1));
  });
  WaitPublished();
  endpoints[0]->Stop();
  EXPECT_EQ(active.get().status().code(), absl::StatusCode::kCancelled);
  trace->hold_pdo = false;
  EXPECT_TRUE(endpoints[1]->Exchange(JwecRequest(2, JW_CMD_GET_FEEDBACK), absl::Seconds(1)).ok());
  std::weak_ptr<EthercatMaster> weak = master;
  master.reset();
  endpoints[0].reset();
  EXPECT_FALSE(weak.expired());
  endpoints[1].reset();
  EXPECT_TRUE(weak.expired());
  EXPECT_EQ(trace->calls.back(), "destroy");
}

TEST_F(JwecAdapterTest, MailboxTimeoutQuarantinesSessionUntilVerifiedNewReset) {
  trace->hold_mailbox = true;
  EXPECT_EQ(endpoints[0]->Exchange(JwecRequest(2, JW_CMD_ENABLE)).status().code(),
            absl::StatusCode::kDeadlineExceeded);
  // A read in progress at the overall deadline may conservatively fault the
  // master. If still running, no subsequent command may bypass a new reset.
  if (master->status().ok()) {
    EXPECT_EQ(endpoints[0]->Exchange(JwecRequest(3, JW_CMD_ENABLE)).status().code(),
              absl::StatusCode::kFailedPrecondition);
    trace->hold_mailbox = false;
    EXPECT_TRUE(endpoints[0]->Exchange(JwecRequest(1, JW_CMD_RESET_SESSION, 8)).ok());
    EXPECT_FALSE(endpoints[0]->Exchange(JwecRequest(4, JW_CMD_ENABLE, 7)).ok());
    EXPECT_TRUE(endpoints[0]->Exchange(JwecRequest(2, JW_CMD_ENABLE, 8)).ok());
  }
}

TEST_F(JwecAdapterTest, BusFailureWakesBothPlanes) {
  allow_shutdown_failure = true;
  trace->hold_pdo = true;
  auto active = std::async(std::launch::async, [&] {
    return endpoints[0]->Exchange(JwecRequest(2, JW_CMD_GET_FEEDBACK), absl::Seconds(1));
  });
  WaitPublished();
  trace->bad_wkc = true;
  EXPECT_FALSE(active.get().ok());
  EXPECT_FALSE(endpoints[1]->Exchange(JwecRequest(2, JW_CMD_IDENTIFY)).ok());
  // Restore WKC for the owner's best-effort final exchange.
  trace->bad_wkc = false;
}

TEST_F(JwecAdapterTest, MalformedMailboxRequiresResetAndConcurrentCyclicCallsSerialize) {
  trace->mutation = 4;
  EXPECT_EQ(endpoints[0]->Exchange(JwecRequest(2, JW_CMD_IDENTIFY)).status().code(),
            absl::StatusCode::kDataLoss);
  EXPECT_EQ(endpoints[0]->Exchange(JwecRequest(3, JW_CMD_ENABLE)).status().code(),
            absl::StatusCode::kFailedPrecondition);
  trace->mutation = 0;
  ASSERT_TRUE(endpoints[0]->Exchange(JwecRequest(1, JW_CMD_RESET_SESSION, 8)).ok());
  trace->hold_pdo = true;
  auto first = std::async(std::launch::async, [&] {
    return endpoints[0]->Exchange(JwecRequest(2, JW_CMD_GET_FEEDBACK, 8), absl::Seconds(1));
  });
  WaitPublished();
  auto second = std::async(std::launch::async, [&] {
    return endpoints[0]->Exchange(JwecRequest(3, JW_CMD_GET_FEEDBACK, 8), absl::Seconds(1));
  });
  EXPECT_EQ(second.wait_for(10ms), std::future_status::timeout);
  trace->hold_pdo = false;
  EXPECT_TRUE(first.get().ok());
  EXPECT_TRUE(second.get().ok());
  for (auto& e : endpoints) e->Stop();
  EXPECT_TRUE(master->Stop().ok());
  ASSERT_EQ(trace->pdo_requests.size(), 2);
  EXPECT_EQ(trace->pdo_requests[0], JwecRequest(2, JW_CMD_GET_FEEDBACK, 8));
  EXPECT_EQ(trace->pdo_requests[1], JwecRequest(3, JW_CMD_GET_FEEDBACK, 8));
}

// End-to-end wire contract: the real host adapters talk to the exact portable
// C firmware core used by the AM243 artifact, not a second protocol simulator.
struct FirmwareProfileTrace : IoTrace {
  FirmwareProfileTrace() {
    outputs.assign(160, 0);
    for (auto& profile : profiles)
      EXPECT_EQ(JoshuaEthercatProfileInit(&profile, 2000000, 1000000), 0);
  }
  JoshuaEthercatProfile profiles[2];
  uint16_t incompatible_slave = 0;  // Set before opening the owner.
  std::atomic<int> starts{0};
  std::atomic<int> teardowns{0};
};
class FirmwareProfileIo : public TestMasterIo {
 public:
  explicit FirmwareProfileIo(std::shared_ptr<FirmwareProfileTrace> trace)
      : TestMasterIo(trace), trace_(std::move(trace)) {}
  bool HasIncrementalSdo() const override {
    return true;
  }
  absl::StatusOr<PdoRegion> GetPdoRegion(uint16_t slave) const override {
    return PdoRegion{slave, size_t((slave - 1) * 80), size_t((slave - 1) * 80), 80, 80};
  }
  absl::Status StartCyclic() override {
    trace_->Call("start");
    ++trace_->starts;
    operational_ = true;
    return absl::OkStatus();
  }
  absl::Status StopCyclic() override {
    trace_->Call("stop");
    operational_ = false;
    return absl::OkStatus();
  }
  absl::Status Teardown() override {
    ++trace_->teardowns;
    return TestMasterIo::Teardown();
  }
  absl::StatusOr<SdoBytes> ReadSdo(SdoAddress a, size_t capacity, int) override {
    trace_->Call("read_profile");
    Tick();
    SdoBytes result(capacity);
    if (JoshuaEthercatProfileRead(&trace_->profiles[a.slave - 1], a.index, result.data(), capacity))
      return absl::InvalidArgumentError("firmware rejected SDO read");
    if (a.index == JWEC_DESCRIPTOR_INDEX && a.slave == trace_->incompatible_slave) result[0] = 0;
    return result;
  }
  absl::Status WriteSdo(SdoAddress a, const SdoBytes& bytes, int) override {
    trace_->Call("write_profile");
    Tick();
    if (JoshuaEthercatProfileWrite(
            &trace_->profiles[a.slave - 1], a.index, bytes.data(), bytes.size()))
      return absl::InvalidArgumentError("firmware rejected SDO write");
    return absl::OkStatus();
  }
  absl::Status BeginSdo(SdoAddress a, bool write, SdoBytes bytes, size_t capacity) override {
    address_ = a;
    write_ = write;
    bytes_ = std::move(bytes);
    capacity_ = capacity;
    return absl::OkStatus();
  }
  absl::StatusOr<std::optional<SdoBytes>> StepSdo(int timeout) override {
    if (write_) {
      const auto status = WriteSdo(address_, bytes_, timeout);
      if (!status.ok()) return status;
      return std::optional<SdoBytes>(SdoBytes{});
    }
    auto response = ReadSdo(address_, capacity_, timeout);
    if (!response.ok()) return response.status();
    return std::optional<SdoBytes>(std::move(*response));
  }
  absl::StatusOr<ProcessData> ExchangeProcessData(int) override {
    trace_->Call("exchange");
    Tick();
    SdoBytes input;
    for (size_t i = 0; i < 2; ++i) {
      auto& profile = trace_->profiles[i];
      (void)JoshuaEthercatProfilePdo(&profile, trace_->outputs.data() + i * 80, 80);
      input.insert(input.end(), profile.input, profile.input + 80);
    }
    return ProcessData{trace_->outputs, input, 6, 6};
  }

 private:
  void Tick() {
    const uint64_t now = std::chrono::duration_cast<std::chrono::microseconds>(
                             std::chrono::steady_clock::now().time_since_epoch())
                             .count();
    for (auto& profile : trace_->profiles) JoshuaEthercatProfileTick(&profile, now, operational_);
  }
  std::shared_ptr<FirmwareProfileTrace> trace_;
  bool operational_ = false;
  SdoAddress address_{};
  bool write_ = false;
  SdoBytes bytes_;
  size_t capacity_ = 0;
};

TEST(JwecProfileTest, HostAdaptersInteroperateWithProductionAm243FirmwareCore) {
  auto trace = std::make_shared<FirmwareProfileTrace>();
  auto opened = EthercatMaster::Open(std::make_unique<FirmwareProfileIo>(trace),
                                     "test-only",
                                     ProcessDataMode::kSplitLrdLwr,
                                     RuntimeOptions());
  ASSERT_TRUE(opened.ok());
  std::shared_ptr<EthercatMaster> master(std::move(*opened));
  auto connected = JoshuaWireEthercatTransport::Open(master, 1, {1s, 5ms});
  ASSERT_TRUE(connected.ok()) << connected.status();
  auto endpoint = *connected;
  ASSERT_TRUE(endpoint->Exchange(JwecRequest(1, JW_CMD_RESET_SESSION)).ok());
  auto identity = endpoint->Exchange(JwecRequest(2, JW_CMD_IDENTIFY));
  ASSERT_TRUE(identity.ok()) << identity.status();
  jw2_frame_t frame;
  ASSERT_EQ(jw2_decode_frame(identity->data(), identity->size(), &frame), 0);
  jw_identify_response_t value;
  ASSERT_EQ(jw_decode_identify_payload(frame.payload, frame.payload_len, &value), 0);
  EXPECT_EQ(value.board_id, JW_BOARD_AM243);
  EXPECT_EQ(std::string(value.fw_name), "am243-ec-v2");
  ASSERT_TRUE(master
                  ->StartCyclic({JoshuaWireEthercatTransport::StopImage(),
                                 JoshuaWireEthercatTransport::StopImage()})
                  .ok());
  auto command = [&](uint32_t id, uint8_t cmd, const SdoBytes& payload = SdoBytes{}) {
    SdoBytes bytes(64);
    bytes.resize(jw2_encode_frame(
        bytes.data(), bytes.size(), 7, id, cmd, 0, payload.data(), payload.size()));
    return bytes;
  };
  ASSERT_TRUE(endpoint->Exchange(command(3, JW_CMD_CONFIGURE_CHANNEL, SdoBytes(11))).ok());
  auto enabled = endpoint->Exchange(command(4, JW_CMD_ENABLE));
  ASSERT_TRUE(enabled.ok()) << enabled.status();
  ASSERT_EQ(jw2_decode_frame(enabled->data(), enabled->size(), &frame), 0);
  ASSERT_EQ(frame.payload_len, 1);
  EXPECT_EQ(frame.payload[0], JW_STATUS_OK);
  SdoBytes target(5);
  ASSERT_EQ(jw_encode_set_target_payload(target.data(), target.size(), JW_MODE_POSITION, 123.0f),
            5);
  auto set = endpoint->Exchange(command(5, JW_CMD_SET_TARGET, target), absl::Seconds(1));
  ASSERT_TRUE(set.ok()) << set.status();
  ASSERT_EQ(jw2_decode_frame(set->data(), set->size(), &frame), 0);
  EXPECT_EQ(frame.payload[0], JW_STATUS_OK);
  auto feedback = endpoint->Exchange(command(6, JW_CMD_GET_FEEDBACK), absl::Seconds(1));
  ASSERT_TRUE(feedback.ok()) << feedback.status();
  ASSERT_EQ(jw2_decode_frame(feedback->data(), feedback->size(), &frame), 0);
  jw_feedback_t motion;
  ASSERT_EQ(jw_decode_feedback_payload(frame.payload, frame.payload_len, &motion), 0);
  EXPECT_FLOAT_EQ(motion.position, 123.0f);
  EXPECT_EQ(motion.fault_flags, 0);
  ASSERT_TRUE(endpoint->Exchange(JwecRequest(7, JW_CMD_ESTOP)).ok());
  endpoint->Stop();
  ASSERT_TRUE(master->Stop().ok());
  EXPECT_FALSE(trace->profiles[0].channel.enabled);
  EXPECT_TRUE(trace->profiles[0].channel.estopped);
  for (const auto& thread : trace->threads) EXPECT_EQ(thread, trace->threads.front());
}

// Full config -> BoardFactory -> CommFactory -> adapters -> production firmware
// core, reusing the existing I/O seam. No new smoke utility or hardware access.
robot::board::Board PairedBoard(uint32_t slave = 1) {
  robot::board::Board b;
  b.set_name("jw2_" + std::to_string(slave));
  b.set_board_type(robot::board::AM243);
  b.set_protocol(robot::board::JOSHUA_WIRE_V2);
  b.mutable_firmware()->set_min_proto_version(2);
  auto* c = b.mutable_comm();
  c->set_comm_type(ETHERCAT);
  c->set_transport_type(MESSAGE_AND_CYCLIC);
  auto* ec = c->mutable_ethercat_config();
  ec->set_interface_name("test-only");
  ec->set_slave_index(slave);
  ec->set_process_data_mode(ETHERCAT_PROCESS_DATA_MODE_SPLIT_LRD_LWR);
  auto* t = ec->mutable_timing();
  t->set_period_us(20000);
  t->set_process_timeout_us(1000);
  t->set_state_timeout_us(1000);
  t->set_operation_timeout_us(1000000);
  t->set_mailbox_step_budget_us(1000);
  t->set_scheduling_guard_us(1000);
  t->set_response_timeout_us(1000000);
  auto* channel = b.add_channels();
  channel->set_index(0);
  channel->set_drive(robot::board::STEP_DIR);
  channel->mutable_step_dir()->set_max_pulse_rate_hz(1000);
  return b;
}

class JwecFactoryTest : public ::testing::Test {
 protected:
  void SetUp() override {
    CommFactory::SetEthercatMasterIoFactoryForTesting([this] {
      ++opens;
      return std::make_unique<FirmwareProfileIo>(trace);
    });
  }
  void TearDown() override {
    robot::board::BoardFactory::ResetForTesting();
    CommFactory::ResetEthercatTransportCacheForTesting();
    CommFactory::SetEthercatMasterIoFactoryForTesting(nullptr);
  }
  std::shared_ptr<FirmwareProfileTrace> trace = std::make_shared<FirmwareProfileTrace>();
  int opens = 0;
};

TEST_F(JwecFactoryTest, BoardEngineRoutesBothPlanesAndClosingOnePreservesOtherSlave) {
  auto first = robot::board::BoardFactory::GetOrCreate(PairedBoard());
  ASSERT_TRUE(first.ok()) << first.status();
  auto second = robot::board::BoardFactory::GetOrCreate(PairedBoard(2));
  ASSERT_TRUE(second.ok()) << second.status();
  EXPECT_EQ(opens, 1);
  EXPECT_EQ(trace->starts, 1);
  auto a = (*first)->OpenChannel(0);
  auto b = (*second)->OpenChannel(0);
  ASSERT_TRUE(a.ok());
  ASSERT_TRUE(b.ok());
  ASSERT_TRUE((*a)->Enable().ok());
  ASSERT_TRUE((*a)->SetTarget(robot::board::TargetMode::kPosition, 42).ok());
  auto feedback = (*a)->ReadFeedback();
  ASSERT_TRUE(feedback.ok()) << feedback.status();
  EXPECT_FLOAT_EQ(feedback->position, 42);
  EXPECT_EQ(feedback->fault_flags, 0);
  ASSERT_TRUE((*a)->Disable().ok());
  ASSERT_TRUE((*first)->Teardown().ok());
  EXPECT_EQ(trace->teardowns, 0);
  EXPECT_EQ((*a)->Enable().code(), absl::StatusCode::kFailedPrecondition);
  ASSERT_TRUE((*b)->Enable().ok());
  ASSERT_TRUE((*b)->SetTarget(robot::board::TargetMode::kVelocity, 7).ok());
  feedback = (*b)->ReadFeedback();
  ASSERT_TRUE(feedback.ok());
  EXPECT_FLOAT_EQ(feedback->velocity, 7);
  EXPECT_EQ(feedback->fault_flags, 0);
  ASSERT_TRUE((*second)->Teardown().ok());
  EXPECT_EQ(trace->teardowns, 1);  // Retained board/channel handles hold no NIC lease.
  for (const auto& profile : trace->profiles) {
    EXPECT_FALSE(profile.channel.enabled);
    EXPECT_TRUE(profile.channel.estopped);
  }
  for (const auto& thread : trace->threads) EXPECT_EQ(thread, trace->threads.front());
}

TEST_F(JwecFactoryTest, GatesEverySlaveBeforeOpAndFailureIsNotCached) {
  trace->incompatible_slave = 2;
  auto rejected = CommFactory::CreateComm(PairedBoard().comm());
  EXPECT_EQ(rejected.status().code(), absl::StatusCode::kFailedPrecondition);
  EXPECT_EQ(trace->starts, 0);
  EXPECT_EQ(trace->teardowns, 1);
  trace->incompatible_slave = 0;
  auto retry = CommFactory::CreateComm(PairedBoard().comm());
  ASSERT_TRUE(retry.ok()) << retry.status();
  EXPECT_EQ(opens, 2);
  EXPECT_EQ(trace->starts, 1);
}

TEST_F(JwecFactoryTest, RejectsDuplicateClaimsTimingChangesAndLegacyRequests) {
  auto config = PairedBoard();
  auto first = CommFactory::CreateComm(config.comm());
  ASSERT_TRUE(first.ok()) << first.status();
  EXPECT_EQ(CommFactory::CreateComm(config.comm()).status().code(),
            absl::StatusCode::kAlreadyExists);
  auto other = PairedBoard(2);
  other.mutable_comm()->mutable_ethercat_config()->mutable_timing()->set_period_us(30000);
  EXPECT_EQ(CommFactory::CreateComm(other.comm()).status().code(),
            absl::StatusCode::kInvalidArgument);
  auto legacy = config.comm();
  legacy.set_transport_type(CYCLIC);
  EXPECT_EQ(CommFactory::CreateComm(legacy).status().code(), absl::StatusCode::kInvalidArgument);
  auto second = CommFactory::CreateComm(PairedBoard(2).comm());
  ASSERT_TRUE(second.ok());
  first = absl::CancelledError("release first lease");
  EXPECT_EQ(CommFactory::CreateComm(config.comm()).status().code(),
            absl::StatusCode::kAlreadyExists);
  EXPECT_EQ(trace->teardowns, 0);
  second = absl::CancelledError("release last lease");
  EXPECT_EQ(trace->teardowns, 1);
  auto reopened = CommFactory::CreateComm(config.comm());
  ASSERT_TRUE(reopened.ok()) << reopened.status();
  EXPECT_EQ(opens, 2);
}

TEST_F(JwecFactoryTest, RegionAssertionsFailBeforeOpAndSlaveRangeNeverTruncates) {
  auto config = PairedBoard();
  config.mutable_comm()->mutable_ethercat_config()->set_slave_index(65536);
  EXPECT_EQ(CommFactory::CreateComm(config.comm()).status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(opens, 0);
  config.mutable_comm()->mutable_ethercat_config()->set_slave_index(3);
  EXPECT_EQ(CommFactory::CreateComm(config.comm()).status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(trace->starts, 0);
  config = PairedBoard();
  auto* region = config.mutable_comm()->mutable_ethercat_config()->mutable_pdo_region();
  region->set_output_size_bytes(80);
  region->set_input_size_bytes(80);
  region->set_input_offset_bytes(1);
  EXPECT_EQ(CommFactory::CreateComm(config.comm()).status().code(),
            absl::StatusCode::kFailedPrecondition);
  EXPECT_EQ(trace->starts, 0);
  region->set_input_offset_bytes(0);
  EXPECT_TRUE(CommFactory::CreateComm(config.comm()).ok());
}

TEST_F(JwecFactoryTest, ReopenWaitsForLastOwnerToFinishClosingTheNic) {
  auto first = CommFactory::CreateComm(PairedBoard().comm());
  ASSERT_TRUE(first.ok());
  {
    std::lock_guard lock(trace->mutex);
    trace->hold = "teardown";
  }
  auto closing = std::async(std::launch::async, [owner = std::move(*first)]() mutable {
    owner = std::shared_ptr<MessageTransport>{};
  });
  ASSERT_TRUE(trace->WaitEntered());
  auto reopening =
      std::async(std::launch::async, [] { return CommFactory::CreateComm(PairedBoard().comm()); });
  EXPECT_EQ(reopening.wait_for(10ms), std::future_status::timeout);
  trace->Release();
  closing.get();
  auto second = reopening.get();
  ASSERT_TRUE(second.ok()) << second.status();
  EXPECT_EQ(opens, 2);
  EXPECT_EQ(trace->teardowns, 1);
}

TEST_F(JwecFactoryTest, InvalidBoardPolicyDoesNotOpenNicAndFailedIdentifyReleasesLease) {
  auto config = PairedBoard();
  config.clear_protocol();
  EXPECT_FALSE(robot::board::BoardFactory::GetOrCreate(config).ok());
  config = PairedBoard();
  config.mutable_am243_config()->set_slave_index(1);
  EXPECT_FALSE(robot::board::BoardFactory::GetOrCreate(config).ok());
  EXPECT_EQ(opens, 0);
  config = PairedBoard();
  config.mutable_channels(0)->set_index(1);  // Real firmware exposes channel zero only.
  EXPECT_FALSE(robot::board::BoardFactory::GetOrCreate(config).ok());
  EXPECT_EQ(trace->teardowns, 1);
  auto retry = robot::board::BoardFactory::GetOrCreate(PairedBoard());
  ASSERT_TRUE(retry.ok()) << retry.status();
  EXPECT_EQ(opens, 2);
}

}  // namespace
}  // namespace robot::comm::ethercat
