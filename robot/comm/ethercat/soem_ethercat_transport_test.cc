#include "robot/comm/ethercat/soem_ethercat_transport.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "absl/status/status.h"
#include "gtest/gtest.h"
#include "robot/comm/ethercat/ethercat_master.h"
#include "robot/comm/ethercat/ethercat_transport.h"

namespace robot::comm::ethercat {
namespace {

TEST(SoemEthercatTransportTest, InitRejectsEmptyInterfaceName) {
  SoemEthercatTransport transport;

  auto status = transport.Init("", ProcessDataMode::kSplitLrdLwr);

  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
}

TEST(SoemEthercatTransportTest, InitRejectsLrwMode) {
  SoemEthercatTransport transport;

  auto status = transport.Init("enp5s0", ProcessDataMode::kLrw);

  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
}

TEST(SoemEthercatTransportTest, InitReportsUnavailableForMissingInterface) {
  SoemEthercatTransport transport;

  auto status = transport.Init("joshua-no-such-ethercat-iface0", ProcessDataMode::kSplitLrdLwr);

  EXPECT_EQ(status.code(), absl::StatusCode::kUnavailable);
}

TEST(SoemEthercatTransportTest, TeardownIsOkBeforeInit) {
  SoemEthercatTransport transport;

  EXPECT_TRUE(transport.Teardown().ok());
}

TEST(SoemEthercatTransportTest, ConfigureSlavesRequiresInit) {
  SoemEthercatTransport transport;

  EXPECT_EQ(transport.ConfigureSlaves().code(), absl::StatusCode::kFailedPrecondition);
}

TEST(SoemEthercatTransportTest, StartCyclicRequiresConfiguredSlaves) {
  SoemEthercatTransport transport;

  EXPECT_EQ(transport.StartCyclic().code(), absl::StatusCode::kFailedPrecondition);
}

TEST(SoemEthercatTransportTest, MetadataAccessRequiresConfiguredSlaves) {
  SoemEthercatTransport transport;

  EXPECT_EQ(transport.GetSlaves().status().code(), absl::StatusCode::kFailedPrecondition);
  EXPECT_EQ(transport.GetPdoRegion(2).status().code(), absl::StatusCode::kFailedPrecondition);
}

TEST(SoemEthercatTransportTest, PdoBufferAccessRequiresConfiguredSlaves) {
  SoemEthercatTransport transport;
  PdoRegion region;
  region.slave_index = 2;
  region.output_size_bytes = 8;
  region.input_size_bytes = 8;

  EXPECT_EQ(transport.WriteOutputs(region, std::vector<uint8_t>(8, 0)).code(),
            absl::StatusCode::kFailedPrecondition);
  EXPECT_EQ(transport.ReadInputs(region).status().code(), absl::StatusCode::kFailedPrecondition);
}

TEST(SoemEthercatTransportTest, ExchangeProcessDataRequiresConfiguredSlaves) {
  SoemEthercatTransport transport;

  EXPECT_EQ(transport.ExchangeProcessData().status().code(), absl::StatusCode::kFailedPrecondition);
}

TEST(SoemEthercatTransportTest, StopCyclicIsOkBeforeInit) {
  SoemEthercatTransport transport;

  EXPECT_TRUE(transport.StopCyclic().ok());
}

TEST(SoemEthercatTransportTest, NewIoMethodsFailBeforeAccessingHardware) {
  SoemEthercatTransport transport;
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
  absl::StatusOr<std::vector<uint8_t>> ReadInputs(const PdoRegion&) const override {
    ADD_FAILURE() << "owner must use complete snapshots";
    return absl::UnimplementedError("not used");
  }
  absl::StatusOr<ProcessData> ExchangeProcessData() override {
    ADD_FAILURE() << "owner must supply timeout";
    return absl::UnimplementedError("not used");
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

}  // namespace
}  // namespace robot::comm::ethercat
