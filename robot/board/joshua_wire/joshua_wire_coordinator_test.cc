// Transport-independent protocol fixture. No devices or serial implementation.
#include "robot/board/joshua_wire/joshua_wire_coordinator.h"

#include <fcntl.h>
#include <poll.h>
#include <stdlib.h>
#include <unistd.h>

#include <atomic>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <future>
#include <limits>
#include <mutex>
#include <thread>
#include <vector>

#include "firmware/common/joshua_wire.h"
#include "firmware/common/joshua_wire_profiles.h"
#include "firmware/common/joshua_wire_sensor_stream.h"
#include "firmware/common/joshua_wire_stream.h"
#include "gtest/gtest.h"
#include "robot/comm/factory/comm_factory.h"

namespace robot::board {
namespace {
using namespace std::chrono_literals;
using Bytes = std::vector<uint8_t>;
using Link = robot::comm::FrameStreamTransport;
using Coordinator = JoshuaWireCoordinator;
using Clock = Link::Clock;
class MemoryLink : public Link {
 public:
  mutable std::mutex mutex;
  std::condition_variable changed;
  std::deque<ReceivedFrame> incoming;
  std::vector<Bytes> writes;
  bool interrupted = false;
  absl::Status failure;
  uint8_t blocked_command = 0;
  uint8_t blocked_operation = 255;
  unsigned drop = 0;
  std::function<bool(const jw_frame_t&)> on_write;
  static Bytes Reply(const jw_frame_t& request, jw_execution_t execution = JW_EXECUTION_COMPLETE) {
    Bytes out(JW_MAX_FRAME_LEN);
    const uint8_t payload[] = {JW_STATUS_OK, static_cast<uint8_t>(execution)};
    out.resize(jw_encode_response(out.data(), out.size(), &request, payload, 2));
    return out;
  }
  WriteResult WriteFrame(absl::Span<const uint8_t> bytes, Clock::time_point deadline) override {
    std::unique_lock lock(mutex);
    if (!failure.ok()) return {failure, 0};
    if (Clock::now() >= deadline) return {absl::DeadlineExceededError("Memory write expired"), 0};
    jw_frame_t frame{};
    if (jw_decode_frame(bytes.data(), bytes.size(), &frame) != JW_RESULT_OK)
      return {absl::DataLossError("Invalid test frame"), 0};
    writes.emplace_back(bytes.begin(), bytes.end());
    const bool handled = on_write && on_write(frame);
    jw_profile_request_t profile{};
    const bool blocked =
        frame.cmd == blocked_command ||
        (frame.cmd == JW_CMD_PROFILE &&
         jw_decode_profile_request(frame.payload, frame.payload_len, &profile) == JW_RESULT_OK &&
         profile.operation == blocked_operation);
    if (drop)
      --drop;
    else if (!handled && !blocked)
      incoming.push_back({Reply(frame), Clock::now()});
    lock.unlock();
    changed.notify_all();
    return {absl::OkStatus(), bytes.size()};
  }
  absl::StatusOr<ReceivedFrame> Receive(Clock::time_point deadline) override {
    std::unique_lock lock(mutex);
    if (!changed.wait_until(
            lock, deadline, [&] { return !failure.ok() || interrupted || !incoming.empty(); }))
      return absl::DeadlineExceededError("Memory receive expired");
    if (!failure.ok()) return failure;
    if (interrupted) {
      interrupted = false;
      return absl::CancelledError("wake");
    }
    auto frame = std::move(incoming.front());
    incoming.pop_front();
    return frame;
  }
  void Interrupt() override {
    {
      std::lock_guard lock(mutex);
      interrupted = true;
    }
    changed.notify_all();
  }
  absl::Status status() const override {
    std::lock_guard lock(mutex);
    return failure;
  }
  absl::Status Close() override {
    {
      std::lock_guard lock(mutex);
      if (failure.ok()) failure = absl::CancelledError("Memory link closed");
    }
    changed.notify_all();
    return absl::OkStatus();
  }
  Diagnostics SnapshotFrames() const override {
    return {};
  }
  void Inject(Bytes bytes) {
    {
      std::lock_guard lock(mutex);
      incoming.push_back({std::move(bytes), Clock::now()});
    }
    changed.notify_all();
  }
  bool WaitWrites(size_t size) {
    std::unique_lock lock(mutex);
    return changed.wait_for(lock, 2s, [&] { return writes.size() >= size; });
  }
  Bytes Written(size_t index) {
    std::lock_guard lock(mutex);
    return writes.at(index);
  }
  void Fault() {
    {
      std::lock_guard lock(mutex);
      failure = absl::UnavailableError("Disconnected");
    }
    changed.notify_all();
  }
};

class CoordinatorTest : public ::testing::Test {
 protected:
  std::shared_ptr<MemoryLink> link = std::make_shared<MemoryLink>();
  std::shared_ptr<Coordinator> coordinator;
  JoshuaWireHostPolicy policy;
  void SetUp() override {
    policy.set_operation_timeout_ms(2000);
    policy.set_attempt_timeout_ms(300);
    policy.set_polling_timeout_ms(1000);
  }
  void Open() {
    auto next = std::make_shared<uint32_t>(40);
    auto opened = Coordinator::Acquire(link, policy, [next] { return ++*next; });
    ASSERT_TRUE(opened.ok()) << opened.status();
    coordinator = *opened;
  }
  void TearDown() override {
    if (coordinator) coordinator->Close().IgnoreError();
  }
  static jw_command_t Command(uint8_t code, uint8_t function = JW_CHANNEL_NONE) {
    static const uint8_t domain[] = {0, 0};
    return {0,
            0,
            code,
            function,
            code == JW_CMD_ESTOP ? domain : nullptr,
            code == JW_CMD_ESTOP ? 2u : 0u};
  }
  static Bytes ProfilePayload(uint8_t operation, uint16_t profile = JW_PROFILE_ACTUATOR) {
    Bytes bytes(JW_PROFILE_HEADER_LEN);
    jw_profile_request_t request{profile, 1, 0, operation, nullptr, 0};
    EXPECT_EQ(jw_encode_profile_request(bytes.data(), bytes.size(), &request), bytes.size());
    return bytes;
  }
  Coordinator::Operation Profile(uint8_t op) {
    auto payload = ProfilePayload(op);
    return coordinator->Submit({0, 0, JW_CMD_PROFILE, 2, payload.data(), payload.size()});
  }
  void Respond(size_t index, jw_execution_t execution = JW_EXECUTION_COMPLETE) {
    auto bytes = link->Written(index);
    jw_frame_t frame{};
    ASSERT_EQ(jw_decode_frame(bytes.data(), bytes.size(), &frame), JW_RESULT_OK);
    link->Inject(MemoryLink::Reply(frame, execution));
  }
  static Bytes Sample(uint32_t session, uint32_t token, uint32_t sequence) {
    jw_stream_sample_t s{};
    s.session_id = session;
    s.function_id = 2;
    s.subscription_id = token;
    s.sample.value = 42;
    s.sample.metadata = {JW_SAMPLE_VALID, JW_CLOCK_BOOT_MONOTONIC_US, 9, sequence, sequence};
    Bytes bytes(JW_STREAM_SAMPLE_FRAME_LEN);
    EXPECT_EQ(jw_encode_stream_sample(bytes.data(), bytes.size(), &s), bytes.size());
    return bytes;
  }
};
TEST_F(CoordinatorTest, RepeatedAcquireSharesOneSessionAndEffectivePolicy) {
  Open();
  auto second = Coordinator::Acquire(link, policy);
  ASSERT_TRUE(second.ok()) << second.status();
  EXPECT_EQ(*second, coordinator);
  EXPECT_EQ(coordinator->Snapshot().resets, 1);
  auto conflict = policy;
  conflict.set_max_priority_burst(9);
  EXPECT_TRUE(absl::IsInvalidArgument(Coordinator::Acquire(link, conflict).status()));
  EXPECT_TRUE(coordinator->Exchange(Command(JW_CMD_IDENTIFY)).ok());
  ASSERT_TRUE(link->WaitWrites(2));
  auto bytes = link->Written(1);
  jw_frame_t frame{};
  ASSERT_EQ(jw_decode_frame(bytes.data(), bytes.size(), &frame), JW_RESULT_OK);
  EXPECT_EQ(frame.session_id, 41);
  EXPECT_EQ(frame.message_id, 2);
}
TEST_F(CoordinatorTest, StopHasReservedCapacityAndRetiresPublishedOrdinaryWork) {
  policy.set_max_pending_operations(1);
  policy.set_max_pending_bytes(192);
  Open();
  {
    std::lock_guard lock(link->mutex);
    link->blocked_command = JW_CMD_PROFILE;
  }
  auto target = Profile(JW_ACTUATOR_SET_TARGET);
  ASSERT_TRUE(link->WaitWrites(2));
  auto rejected = Profile(JW_ACTUATOR_CONFIGURE).Wait();
  EXPECT_TRUE(absl::IsResourceExhausted(rejected.status));
  EXPECT_FALSE(rejected.published);
  auto stop = coordinator->Submit(Command(JW_CMD_ESTOP));
  auto stopped = stop.Wait();
  EXPECT_TRUE(stopped.status.ok()) << stopped.status;
  auto retired = target.Wait();
  EXPECT_TRUE(absl::IsCancelled(retired.status));
  EXPECT_TRUE(retired.published);
  EXPECT_GT(stopped.message_id, retired.message_id);
  EXPECT_EQ(coordinator->Snapshot().retired, 1);
  EXPECT_TRUE(coordinator->Snapshot().uncertain);
  Respond(1);  // Retired late reply must not satisfy a subsequent query.
  EXPECT_TRUE(coordinator->Exchange(Command(JW_CMD_GET_STATUS)).ok());
  EXPECT_TRUE(absl::IsFailedPrecondition(Profile(JW_ACTUATOR_ENABLE).Wait().status));
  EXPECT_EQ(coordinator->Snapshot().retries, 0);
}
TEST_F(CoordinatorTest, RetriesUseExactlyTheSameBytesAndOneCompleteBudget) {
  policy.set_attempt_timeout_ms(60);
  policy.set_operation_timeout_ms(200);
  policy.set_polling_timeout_ms(150);
  Open();
  {
    std::lock_guard lock(link->mutex);
    link->drop = 1;
  }
  auto result = Profile(JW_ACTUATOR_SET_TARGET).Wait();
  ASSERT_TRUE(result.status.ok()) << result.status;
  ASSERT_TRUE(link->WaitWrites(3));
  EXPECT_EQ(link->Written(1), link->Written(2));
  EXPECT_EQ(coordinator->Snapshot().retries, 1);
  {
    std::lock_guard lock(link->mutex);
    link->blocked_command = JW_CMD_PROFILE;
  }
  const auto start = Clock::now();
  auto failure = Profile(JW_ACTUATOR_SET_TARGET).Wait();
  EXPECT_TRUE(absl::IsDeadlineExceeded(failure.status));
  EXPECT_TRUE(failure.published);
  EXPECT_LT(Clock::now() - start, 1s);
  EXPECT_TRUE(absl::IsFailedPrecondition(Profile(JW_ACTUATOR_ENABLE).Wait().status));
}
TEST_F(CoordinatorTest, QueuedCancellationAndExpiryDoNotPublishOrConsumeIds) {
  policy.set_polling_timeout_ms(40);
  Open();
  {
    std::lock_guard lock(link->mutex);
    link->blocked_command = JW_CMD_PROFILE;
  }
  auto target = Profile(JW_ACTUATOR_SET_TARGET);
  ASSERT_TRUE(link->WaitWrites(2));
  auto canceled = Profile(JW_ACTUATOR_CONFIGURE);
  canceled.Cancel();
  auto c = canceled.Wait();
  EXPECT_TRUE(absl::IsCancelled(c.status));
  EXPECT_FALSE(c.published);
  auto expired = coordinator->Submit(Command(JW_CMD_GET_STATUS)).Wait();
  EXPECT_TRUE(absl::IsDeadlineExceeded(expired.status));
  EXPECT_FALSE(expired.published);
  EXPECT_EQ(expired.message_id, 0);
  EXPECT_FALSE(target.Ready());
  Respond(1);
  EXPECT_TRUE(target.Wait().status.ok());
  auto next = Profile(JW_ACTUATOR_CONFIGURE);
  ASSERT_TRUE(link->WaitWrites(3));
  Respond(2);
  EXPECT_EQ(next.Wait().message_id, 3);
}
TEST_F(CoordinatorTest, StopInterlockBlocksQueuedMutationEvenWhenOrdinaryOutcomeIsKnown) {
  Open();
  EXPECT_TRUE(Profile(JW_ACTUATOR_SET_TARGET).Wait().status.ok());
  EXPECT_TRUE(coordinator->Exchange(Command(JW_CMD_ESTOP)).ok());
  EXPECT_TRUE(coordinator->Snapshot().safety_interlock);
  EXPECT_FALSE(coordinator->Snapshot().uncertain);
  const auto result = Profile(JW_ACTUATOR_ENABLE).Wait();
  EXPECT_TRUE(absl::IsFailedPrecondition(result.status));
  EXPECT_FALSE(result.published);
  EXPECT_TRUE(coordinator->Exchange(Command(JW_CMD_GET_STATUS)).ok());
}

TEST_F(CoordinatorTest, SamplesCannotSatisfyRepliesEvenWithIdenticalIdsAndRemainBounded) {
  policy.set_max_notifications(2);
  Open();
  {
    std::lock_guard lock(link->mutex);
    link->blocked_command = JW_CMD_PROFILE;
  }
  auto target = Profile(JW_ACTUATOR_SET_TARGET);
  ASSERT_TRUE(link->WaitWrites(2));
  for (uint32_t seq = 1; seq <= 10; ++seq) link->Inject(Sample(41, 2, seq));
  link->Inject(Sample(40, 2, 11));
  std::this_thread::sleep_for(10ms);
  EXPECT_FALSE(target.Ready());
  Respond(1);
  ASSERT_TRUE(target.Wait().status.ok());
  const auto frames = coordinator->DrainNotifications();
  ASSERT_EQ(frames.size(), 1);
  EXPECT_EQ(frames.front().bytes, Sample(41, 2, 10));
  EXPECT_EQ(coordinator->Snapshot().notifications_dropped, 9);
  EXPECT_EQ(coordinator->Snapshot().stale_notifications, 1);
}
TEST_F(CoordinatorTest, PollingUsesAvailableSlackAfterPriorityBurst) {
  policy.set_max_priority_burst(1);
  Open();
  {
    std::lock_guard lock(link->mutex);
    link->blocked_operation = JW_ACTUATOR_SET_TARGET;
  }
  auto first = Profile(JW_ACTUATOR_SET_TARGET);
  ASSERT_TRUE(link->WaitWrites(2));
  auto control = Profile(JW_ACTUATOR_DISABLE);
  auto poll = coordinator->Submit(Command(JW_CMD_GET_STATUS));
  Respond(1);
  EXPECT_TRUE(first.Wait().status.ok());
  EXPECT_TRUE(poll.Wait().status.ok());
  EXPECT_TRUE(control.Wait().status.ok());
  auto bytes = link->Written(2);
  jw_frame_t frame{};
  ASSERT_EQ(jw_decode_frame(bytes.data(), bytes.size(), &frame), JW_RESULT_OK);
  EXPECT_EQ(frame.cmd, JW_CMD_GET_STATUS);
}
TEST_F(CoordinatorTest, DisconnectInvalidatesEveryWaiterAndNeverReopensOldLease) {
  Open();
  {
    std::lock_guard lock(link->mutex);
    link->blocked_command = JW_CMD_PROFILE;
  }
  auto target = Profile(JW_ACTUATOR_SET_TARGET);
  ASSERT_TRUE(link->WaitWrites(2));
  auto queued = Profile(JW_ACTUATOR_CONFIGURE);
  link->Fault();
  EXPECT_TRUE(absl::IsUnavailable(target.Wait().status));
  EXPECT_TRUE(absl::IsUnavailable(queued.Wait().status));
  EXPECT_TRUE(coordinator->Snapshot().faulted);
  EXPECT_FALSE(Coordinator::Acquire(link, policy).ok());
  EXPECT_FALSE(coordinator->Exchange(Command(JW_CMD_IDENTIFY)).ok());
}
TEST_F(CoordinatorTest, CloseRetiresWaitersResetsDisabledAndIsIdempotent) {
  Open();
  {
    std::lock_guard lock(link->mutex);
    link->blocked_command = JW_CMD_PROFILE;
  }
  auto target = Profile(JW_ACTUATOR_SET_TARGET);
  ASSERT_TRUE(link->WaitWrites(2));
  auto queued = Profile(JW_ACTUATOR_CONFIGURE);
  ASSERT_TRUE(coordinator->Close().ok());
  EXPECT_TRUE(absl::IsCancelled(target.Wait().status));
  EXPECT_TRUE(absl::IsCancelled(queued.Wait().status));
  EXPECT_TRUE(coordinator->Close().ok());
  EXPECT_TRUE(coordinator->Snapshot().closed);
  auto bytes = link->Written(2);
  jw_frame_t frame{};
  ASSERT_EQ(jw_decode_frame(bytes.data(), bytes.size(), &frame), JW_RESULT_OK);
  EXPECT_EQ(frame.cmd, JW_CMD_RESET_SESSION);
  EXPECT_NE(frame.session_id, 41);
  EXPECT_FALSE(Coordinator::Acquire(link, policy).ok());
}
class SubscriptionTest : public CoordinatorTest {
 protected:
  jw_stream_state_t remote{};
  jw_sensor_schema_t schema{
      JW_SENSOR_INTERFACE_SCALAR_F32, JW_UNIT_CELSIUS, JW_CLOCK_BOOT_MONOTONIC_US, 1000000};
  uint32_t boot = 9;
  bool older = false, early = true, malformed_create = false;
  std::atomic<bool> lose_create{false}, lose_cancel{false};
  std::atomic<int> creates{0}, cancels{0}, actuator_mutations{0};
  static Bytes EncodedSample(uint32_t session,
                             uint32_t token,
                             uint32_t sequence,
                             uint32_t boot_id = 9,
                             float value = 42,
                             jw_sample_validity_t validity = JW_SAMPLE_VALID,
                             jw_clock_t clock = JW_CLOCK_BOOT_MONOTONIC_US) {
    jw_stream_sample_t sample{};
    sample.session_id = session;
    sample.subscription_id = token;
    sample.function_id = 2;
    sample.sample.value = value;
    sample.sample.metadata = {validity, clock, boot_id, uint64_t(sequence) * 100, sequence};
    Bytes frame(JW_STREAM_SAMPLE_FRAME_LEN);
    EXPECT_EQ(jw_encode_stream_sample(frame.data(), frame.size(), &sample), frame.size());
    return frame;
  }
  void SetPeer(const std::shared_ptr<MemoryLink>& peer) {
    peer->on_write = [this, peer_raw = peer.get()](const jw_frame_t& request) {
      Bytes data(JW_MAX_PAYLOAD_LEN);
      int size = 0;
      const jw_function_descriptor_t descriptor{2,
                                                3,
                                                JW_PROFILE_SENSOR,
                                                1,
                                                static_cast<uint8_t>(older ? 0 : 1),
                                                older ? 1u : JW_SENSOR_STREAM_OPERATIONS,
                                                0};
      if (request.cmd == JW_CMD_RESET_SESSION) {
        remote = {};
        return false;
      }
      if (request.cmd == JW_CMD_IDENTIFY) {
        jw_identify_response_t identity{};
        identity.boot_id = boot;
        identity.discovery_generation = 1;
        identity.function_count = 1;
        identity.max_frame_len = JW_MAX_FRAME_LEN;
        identity.max_inflight = 1;
        identity.max_page_entries = JW_DISCOVERY_PAGE_SIZE;
        identity.max_handler_us = 1000;
        identity.safety_flags = JW_SAFETY_BOARD_ESTOP;
        identity.protocol_min = identity.protocol_max = JW_PROTO_VERSION;
        identity.supported_planes = JW_PLANE_MESSAGE;
        size = jw_encode_identify_payload(data.data(), data.size(), &identity);
      } else if (request.cmd == JW_CMD_DISCOVER) {
        jw_discovery_page_t page{};
        page.generation = 1;
        page.total = 1;
        page.next_cursor = JW_CURSOR_END;
        page.count = 1;
        page.functions[0] = descriptor;
        size = jw_encode_discovery_page(data.data(), data.size(), &page);
      } else if (request.cmd == JW_CMD_DESCRIBE_FUNCTION) {
        size = jw_encode_function_descriptor(data.data(), data.size(), &descriptor);
        size += jw_encode_sensor_schema(data.data() + size, data.size() - size, &schema);
      } else if (request.cmd == JW_CMD_PROFILE) {
        jw_profile_request_t profile{};
        if (jw_decode_profile_request(request.payload, request.payload_len, &profile) !=
            JW_RESULT_OK)
          return false;
        if (profile.profile_id != JW_PROFILE_SENSOR) {
          ++actuator_mutations;
          return false;
        }
        if (profile.operation == JW_SENSOR_SUBSCRIBE) {
          uint32_t period;
          if (jw_decode_stream_period(profile.data, profile.data_len, &period) != JW_RESULT_OK)
            return false;
          if (!remote.active) {
            remote = {1, request.message_id, period};
            ++creates;
          }
          if (early)
            peer_raw->incoming.push_back(
                {EncodedSample(request.session_id, remote.subscription_id, 1, boot), Clock::now()});
          if (lose_create) return true;
        } else if (profile.operation == JW_SENSOR_UNSUBSCRIBE) {
          if (remote.active) {
            remote.active = 0;
            ++cancels;
          }
          if (lose_cancel) return true;
        } else if (profile.operation != JW_SENSOR_GET_SUBSCRIPTION)
          return false;
        auto state = remote;
        if (malformed_create && profile.operation == JW_SENSOR_SUBSCRIBE) ++state.subscription_id;
        size = jw_encode_stream_state(data.data(), data.size(), &state);
      } else
        return false;
      if (size < 0) return false;
      Bytes payload{JW_STATUS_OK, JW_EXECUTION_COMPLETE};
      payload.insert(payload.end(), data.begin(), data.begin() + size);
      Bytes frame(JW_MAX_FRAME_LEN);
      frame.resize(
          jw_encode_response(frame.data(), frame.size(), &request, payload.data(), payload.size()));
      peer_raw->incoming.push_back({std::move(frame), Clock::now()});
      return true;
    };
  }
  void OpenSensor() {
    policy.set_telemetry_bytes_per_second(10000);
    SetPeer(link);
    Open();
  }
  void Emit(uint32_t sequence,
            uint32_t boot_id = 9,
            float value = 42,
            jw_sample_validity_t validity = JW_SAMPLE_VALID,
            jw_clock_t clock = JW_CLOCK_BOOT_MONOTONIC_US) {
    const auto state = coordinator->QuerySubscription(2);
    ASSERT_TRUE(state.ok()) << state.status();
    link->Inject(EncodedSample(coordinator->Snapshot().session_id,
                               state->subscription_id,
                               sequence,
                               boot_id,
                               value,
                               validity,
                               clock));
  }
  bool WaitNotifications(uint64_t count) {
    const auto until = Clock::now() + 1s;
    while (Clock::now() < until) {
      if (coordinator->Snapshot().notifications >= count) return true;
      std::this_thread::sleep_for(1ms);
    }
    return false;
  }
};
TEST_F(SubscriptionTest, CompatibleConsumersShareOneSubscriptionAndEarlyAcquisition) {
  OpenSensor();
  auto first = coordinator->Subscribe(2, 10000);
  ASSERT_TRUE(first.ok()) << first.status();
  auto second = coordinator->Subscribe(2, 10000);
  ASSERT_TRUE(second.ok()) << second.status();
  EXPECT_EQ(creates, 1);
  EXPECT_EQ(coordinator->Snapshot().active_streams, 1);
  auto sample = (*second)->Latest();
  ASSERT_TRUE(sample.ok()) << sample.status();
  EXPECT_TRUE(sample->usable);
  EXPECT_EQ(sample->schema.unit, JW_UNIT_CELSIUS);
  EXPECT_EQ(sample->sample.sample.value, 42);
  EXPECT_EQ(sample->sample.sample.metadata.boot_id, 9);
  EXPECT_TRUE((*first)->Close().ok());
  EXPECT_EQ(cancels, 0);
  EXPECT_TRUE((*second)->Latest().ok());
  EXPECT_FALSE((*first)->Latest().ok());
  EXPECT_TRUE((*second)->Close().ok());
  EXPECT_EQ(cancels, 1);
  EXPECT_EQ(coordinator->Snapshot().active_streams, 0);
  EXPECT_EQ(coordinator->Snapshot().retired_streams, 0);
}
TEST_F(SubscriptionTest, PeriodConflictAndInfeasibleReservationNeverCreateAnotherSubscription) {
  OpenSensor();
  auto first = coordinator->Subscribe(2, 10000);
  ASSERT_TRUE(first.ok()) << first.status();
  EXPECT_TRUE(absl::IsInvalidArgument(coordinator->Subscribe(2, 11000).status()));
  EXPECT_EQ(creates, 1);
  (*first)->Close().IgnoreError();
  EXPECT_TRUE(absl::IsResourceExhausted(coordinator->Subscribe(2, 1000).status()));
  EXPECT_EQ(creates, 1);
}
TEST_F(SubscriptionTest, OlderFirmwareIsRejectedBeforeSubscribeMutation) {
  older = true;
  OpenSensor();
  EXPECT_TRUE(absl::IsUnimplemented(coordinator->Subscribe(2, 10000).status()));
  EXPECT_EQ(creates, 0);
}
TEST_F(SubscriptionTest, RepeatedAcquisitionDoesNotRenewReceiptFreshness) {
  schema.max_age_us = 20000;
  OpenSensor();
  auto subscription = coordinator->Subscribe(2, 10000);
  ASSERT_TRUE(subscription.ok()) << subscription.status();
  auto initial = (*subscription)->Latest();
  ASSERT_TRUE(initial.ok()) << initial.status();
  const auto count = coordinator->Snapshot().notifications;
  std::this_thread::sleep_for(40ms);
  Emit(1);
  ASSERT_TRUE(WaitNotifications(count + 1));
  auto repeated = (*subscription)->Latest();
  ASSERT_TRUE(repeated.ok());
  EXPECT_EQ(repeated->received_at, initial->received_at);
  EXPECT_FALSE(repeated->usable);
  EXPECT_FALSE(repeated->receipt_fresh);
  EXPECT_EQ(repeated->sample.sample.metadata.sequence, 1);
}
TEST_F(SubscriptionTest, SequenceLossRegressionClockAndInvalidMeasurementsRemainObservable) {
  OpenSensor();
  auto subscription = coordinator->Subscribe(2, 10000);
  ASSERT_TRUE(subscription.ok());
  auto count = coordinator->Snapshot().notifications;
  Emit(5);
  ASSERT_TRUE(WaitNotifications(++count));
  auto latest = (*subscription)->Latest();
  ASSERT_TRUE(latest.ok());
  EXPECT_EQ(latest->sequence_gaps, 3);
  Emit(3);
  ASSERT_TRUE(WaitNotifications(++count));
  EXPECT_EQ((*subscription)->Latest()->sample.sample.metadata.sequence, 5);
  Emit(6, 9, 42, JW_SAMPLE_VALID, JW_CLOCK_SYNC_US);
  ASSERT_TRUE(WaitNotifications(++count));
  EXPECT_EQ((*subscription)->Latest()->sample.sample.metadata.sequence, 5);
  Emit(7, 9, std::numeric_limits<float>::quiet_NaN(), JW_SAMPLE_INVALID);
  ASSERT_TRUE(WaitNotifications(++count));
  latest = (*subscription)->Latest();
  ASSERT_TRUE(latest.ok());
  EXPECT_FALSE(latest->usable);
  EXPECT_EQ(latest->sample.sample.metadata.validity, JW_SAMPLE_INVALID);
  EXPECT_TRUE(std::isnan(latest->sample.sample.value));
  EXPECT_EQ(coordinator->Snapshot().invalid_samples, 2);
}
TEST_F(SubscriptionTest, LostCreateReplyReservesIdentityAndQueriesDoNotClearUncertainty) {
  OpenSensor();
  lose_create = true;
  auto subscription = coordinator->Subscribe(2, 10000);
  EXPECT_TRUE(absl::IsDeadlineExceeded(subscription.status()));
  EXPECT_EQ(creates, 1);
  EXPECT_TRUE(coordinator->Snapshot().uncertain);
  auto state = coordinator->QuerySubscription(2);
  ASSERT_TRUE(state.ok());
  EXPECT_TRUE(state->active);
  EXPECT_TRUE(absl::IsFailedPrecondition(coordinator->Subscribe(2, 10000).status()));
  EXPECT_EQ(creates, 1);
  EXPECT_TRUE(coordinator->Snapshot().uncertain);
}
TEST_F(SubscriptionTest, MalformedCreateAcknowledgmentCannotPermitFurtherMutations) {
  malformed_create = true;
  OpenSensor();
  auto subscription = coordinator->Subscribe(2, 10000);
  EXPECT_TRUE(absl::IsDataLoss(subscription.status()));
  EXPECT_EQ(creates, 1);
  EXPECT_TRUE(coordinator->Snapshot().uncertain);
  auto mutation = Profile(JW_ACTUATOR_ENABLE).Wait();
  EXPECT_TRUE(absl::IsFailedPrecondition(mutation.status));
  EXPECT_FALSE(mutation.published);
}
TEST_F(SubscriptionTest, UnknownUnitIsRejectedBeforeCreation) {
  schema.unit = 0x9999;
  OpenSensor();
  EXPECT_TRUE(absl::IsUnimplemented(coordinator->Subscribe(2, 10000).status()));
  EXPECT_EQ(creates, 0);
}

TEST_F(SubscriptionTest, LostCancelReplyRetiresLocalHandleAndNeverCreatesReplacement) {
  OpenSensor();
  auto subscription = coordinator->Subscribe(2, 10000);
  ASSERT_TRUE(subscription.ok());
  lose_cancel = true;
  EXPECT_TRUE(absl::IsDeadlineExceeded((*subscription)->Close()));
  EXPECT_EQ(cancels, 1);
  EXPECT_FALSE((*subscription)->Latest().ok());
  auto state = coordinator->QuerySubscription(2);
  ASSERT_TRUE(state.ok());
  EXPECT_FALSE(state->active);
  EXPECT_TRUE(absl::IsFailedPrecondition(coordinator->Subscribe(2, 10000).status()));
  EXPECT_EQ(creates, 1);
  EXPECT_TRUE(coordinator->Snapshot().uncertain);
}
TEST_F(SubscriptionTest, BootChangeInvalidatesEveryHandleWithoutTargetReplay) {
  OpenSensor();
  auto first = coordinator->Subscribe(2, 10000);
  ASSERT_TRUE(first.ok());
  auto second = coordinator->Subscribe(2, 10000);
  ASSERT_TRUE(second.ok());
  Emit(2, 10);
  const auto until = Clock::now() + 2s;
  while (!coordinator->Snapshot().faulted && Clock::now() < until) std::this_thread::sleep_for(1ms);
  EXPECT_TRUE(coordinator->Snapshot().faulted);
  EXPECT_FALSE((*first)->Latest().ok());
  EXPECT_FALSE((*second)->Latest().ok());
  EXPECT_EQ(coordinator->Snapshot().boot_discontinuities, 1);
  EXPECT_EQ(actuator_mutations, 0);
}
TEST_F(SubscriptionTest, DeliberateReconnectInterlocksValidationAndNeverRevivesOldHandles) {
  OpenSensor();
  auto old = coordinator->Subscribe(2, 10000);
  ASSERT_TRUE(old.ok());
  const auto old_sample = (*old)->Latest();
  ASSERT_TRUE(old_sample.ok());
  auto new_link = std::make_shared<MemoryLink>();
  SetPeer(new_link);
  auto id = std::make_shared<uint32_t>(100);
  auto reconnected = coordinator->Reconnect(
      [new_link]() -> absl::StatusOr<std::shared_ptr<Link>> { return new_link; },
      [](Coordinator& next) {
        EXPECT_TRUE(next.Snapshot().safety_interlock);
        return next.Exchange({0, 0, JW_CMD_IDENTIFY, JW_CHANNEL_NONE, nullptr, 0}).status();
      },
      [id] { return ++*id; });
  ASSERT_TRUE(reconnected.ok()) << reconnected.status();
  EXPECT_FALSE((*old)->Latest().ok());
  EXPECT_EQ(actuator_mutations, 0);
  auto next = (*reconnected)->Subscribe(2, 10000);
  ASSERT_TRUE(next.ok()) << next.status();
  auto current = (*next)->Latest();
  ASSERT_TRUE(current.ok());
  EXPECT_NE(current->sample.session_id, old_sample->sample.session_id);
  new_link->Inject(
      EncodedSample(old_sample->sample.session_id, old_sample->sample.subscription_id, 100));
  const auto until = Clock::now() + 1s;
  while ((*reconnected)->Snapshot().stale_notifications == 0 && Clock::now() < until)
    std::this_thread::sleep_for(1ms);
  EXPECT_EQ((*reconnected)->Snapshot().stale_notifications, 1);
  EXPECT_EQ((*next)->Latest()->sample.session_id, current->sample.session_id);
  EXPECT_TRUE((*reconnected)->Close().ok());
  EXPECT_FALSE((*next)->Latest().ok());
}

TEST_F(SubscriptionTest, FailedReconnectReconciliationClosesTheNewGeneration) {
  OpenSensor();
  auto old = coordinator->Subscribe(2, 10000);
  ASSERT_TRUE(old.ok());
  auto new_link = std::make_shared<MemoryLink>();
  SetPeer(new_link);
  auto result = coordinator->Reconnect(
      [new_link]() -> absl::StatusOr<std::shared_ptr<Link>> { return new_link; },
      [](Coordinator&) {
        return absl::FailedPreconditionError("Device identity does not match configured endpoint");
      });
  EXPECT_TRUE(absl::IsFailedPrecondition(result.status()));
  EXPECT_FALSE(new_link->status().ok());
  EXPECT_FALSE((*old)->Latest().ok());
  EXPECT_EQ(actuator_mutations, 0);
}
TEST(DefaultSessionIdentityTest, RetiredDefaultSessionIdsAreNotReusedByAnotherOwner) {
  auto first_link = std::make_shared<MemoryLink>();
  auto first = Coordinator::Acquire(first_link);
  ASSERT_TRUE(first.ok()) << first.status();
  const auto active = (*first)->Snapshot().session_id;
  ASSERT_TRUE((*first)->Close().ok());
  auto last = first_link->Written(1);
  jw_frame_t reset{};
  ASSERT_EQ(jw_decode_frame(last.data(), last.size(), &reset), JW_RESULT_OK);
  auto second_link = std::make_shared<MemoryLink>();
  auto second = Coordinator::Acquire(second_link);
  ASSERT_TRUE(second.ok()) << second.status();
  EXPECT_NE((*second)->Snapshot().session_id, active);
  EXPECT_NE((*second)->Snapshot().session_id, reset.session_id);
  EXPECT_TRUE((*second)->Close().ok());
}

TEST(CoordinatorSerialTest, ProductionFrameOwnerRoutesInterleavedTelemetryWithoutInputFlush) {
  const int master = posix_openpt(O_RDWR | O_NOCTTY | O_NONBLOCK);
  ASSERT_GE(master, 0);
  ASSERT_EQ(grantpt(master), 0);
  ASSERT_EQ(unlockpt(master), 0);
  robot::comm::Comm config;
  config.set_comm_type(robot::comm::SERIAL);
  config.set_transport_type(robot::comm::FRAME_STREAM);
  auto* serial = config.mutable_serial_config();
  serial->set_port(ptsname(master));
  serial->set_baudrate(115200);
  serial->set_message_adapter(robot::comm::JOSHUA_WIRE);
  auto transport = robot::comm::CommFactory::CreateComm(config);
  ASSERT_TRUE(transport.ok()) << transport.status();
  auto capability = robot::comm::GetCommTransport<Link>(*transport);
  ASSERT_TRUE(capability.ok()) << capability.status();
  auto peer = std::async(std::launch::async, [master] {
    jw_stream_decoder_t decoder;
    jw_stream_init_timeout(&decoder, 1000);
    int replies = 0;
    const auto end = Clock::now() + 5s;
    while (replies < 3 && Clock::now() < end) {
      pollfd descriptor{master, POLLIN, 0};
      if (poll(&descriptor, 1, 1000) <= 0) continue;
      uint8_t byte;
      if (read(master, &byte, 1) != 1) return false;
      const auto ms =
          std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch())
              .count();
      const int length = jw_stream_feed(&decoder, byte, ms);
      if (length <= 0) continue;
      jw_frame_t request{};
      if (jw_decode_frame(decoder.bytes, length, &request) != JW_RESULT_OK) return false;
      if (request.cmd == JW_CMD_IDENTIFY) {
        jw_stream_sample_t sample{};
        sample.session_id = request.session_id;
        sample.subscription_id = request.message_id;
        sample.function_id = 2;
        sample.sample.value = 42;
        sample.sample.metadata = {JW_SAMPLE_VALID, JW_CLOCK_BOOT_MONOTONIC_US, 9, 1, 1};
        uint8_t frame[JW_STREAM_SAMPLE_FRAME_LEN];
        const int size = jw_encode_stream_sample(frame, sizeof(frame), &sample);
        if (write(master, frame, size) != size) return false;
      }
      auto response = MemoryLink::Reply(request);
      if (write(master, response.data(), response.size()) != static_cast<ssize_t>(response.size()))
        return false;
      ++replies;
    }
    return replies == 3;
  });
  JoshuaWireHostPolicy policy;
  policy.set_operation_timeout_ms(2000);
  policy.set_attempt_timeout_ms(500);
  policy.set_polling_timeout_ms(1000);
  auto id = std::make_shared<uint32_t>(10);
  auto owner = Coordinator::Acquire(*capability, policy, [id] { return ++*id; });
  ASSERT_TRUE(owner.ok()) << owner.status();
  auto response = (*owner)->Exchange({0, 0, JW_CMD_IDENTIFY, JW_CHANNEL_NONE, nullptr, 0});
  EXPECT_TRUE(response.ok()) << response.status();
  const auto until = Clock::now() + 2s;
  while ((*owner)->Snapshot().notifications == 0 && Clock::now() < until)
    std::this_thread::sleep_for(1ms);
  EXPECT_EQ((*owner)->DrainNotifications().size(), 1);
  EXPECT_TRUE((*owner)->Close().ok());
  EXPECT_TRUE(peer.get());
  close(master);
}

TEST(CoordinatorPolicyTest, RejectsInvalidBudgetsAndQueueSizesWithoutAConnection) {
  JoshuaWireHostPolicy config;
  config.set_operation_timeout_ms(0);
  EXPECT_TRUE(absl::IsInvalidArgument(ValidateCoordinatorPolicy(ResolveCoordinatorPolicy(config))));
  config.set_operation_timeout_ms(100);
  config.set_max_attempts(5);
  EXPECT_TRUE(absl::IsInvalidArgument(ValidateCoordinatorPolicy(ResolveCoordinatorPolicy(config))));
  config.set_max_attempts(2);
  config.set_max_pending_bytes(191);
  EXPECT_TRUE(absl::IsInvalidArgument(ValidateCoordinatorPolicy(ResolveCoordinatorPolicy(config))));
}

}  // namespace
}  // namespace robot::board
