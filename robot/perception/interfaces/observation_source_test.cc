#include "robot/perception/interfaces/observation_source.h"

#include <future>

#include "gtest/gtest.h"

namespace robot::perception {
namespace {
using namespace std::chrono_literals;
PerceptionPacket Position(float value) {
  PerceptionPacket p;
  p.set_perception_id("joint");
  p.mutable_position()->set_position(value);
  return p;
}
TEST(ObservationStateTest, ValidDataReadinessStallErrorAndRecoveryUseControlledTime) {
  auto t = ObservationState::Clock::time_point{};
  ObservationState state(100ms, t);
  auto first = state.Get(t);
  EXPECT_EQ(first.health.state(), ObservationStatus::STARTING);
  EXPECT_FALSE(first.health.has_last_valid_age_ns());
  EXPECT_EQ(state.Get(t + 100ms).health.state(), ObservationStatus::STALE);
  state.Record(Position(1), t + 110ms);
  EXPECT_EQ(state.Get(t + 120ms).health.state(), ObservationStatus::READY);
  EXPECT_EQ(state.Get(t + 120ms).health.last_valid_age_ns(), 10000000u);
  EXPECT_EQ(state.Get(t + 210ms).health.state(), ObservationStatus::STALE);
  state.Record(absl::UnavailableError("disconnected"), t + 220ms);
  EXPECT_EQ(state.Get(t + 220ms).health.state(), ObservationStatus::ERROR);
  EXPECT_EQ(state.Get(t + 220ms).health.errors(), 1u);
  state.Record(Position(2), t + 230ms);
  auto recovered = state.Get(t + 230ms);
  EXPECT_EQ(recovered.health.state(), ObservationStatus::READY);
  EXPECT_EQ(recovered.health.valid_samples(), 2u);
  EXPECT_EQ(recovered.health.recoveries(), 1u);
  EXPECT_TRUE(recovered.health.has_last_error_steady_ns());
  EXPECT_TRUE(recovered.health.has_last_recovery_steady_ns());
  EXPECT_NE(recovered.health.last_error().find("disconnected"), std::string::npos);
  EXPECT_FLOAT_EQ(recovered.packet->position().position(), 2);
  state.Stop();
  state.Record(Position(3), t + 240ms);
  EXPECT_EQ(state.Get(t + 240ms).health.state(), ObservationStatus::STOPPED);
  EXPECT_FALSE(state.Get(t + 240ms).packet);
}
TEST(ObservationStateTest, RepeatedScalarDoesNotRenewValidityOrHideInvalidAcquisition) {
  auto t = ObservationState::Clock::time_point{};
  ObservationState state(100ms, t);
  PerceptionPacket packet;
  auto* s = packet.mutable_scalar();
  s->set_boot_id(1);
  s->set_session_id(2);
  s->set_subscription_id(3);
  s->set_acquisition_sequence(4);
  s->set_usable(true);
  s->set_receipt_fresh(true);
  state.Record(packet, t);
  state.Record(packet, t + 90ms);
  EXPECT_EQ(state.Get(t + 100ms).health.state(), ObservationStatus::STALE);
  EXPECT_EQ(state.Get(t + 100ms).health.valid_samples(), 1u);
  s->set_acquisition_sequence(5);
  s->set_usable(false);
  state.Record(packet, t + 110ms);
  auto snapshot = state.Get(t + 110ms);
  EXPECT_EQ(snapshot.health.state(), ObservationStatus::INVALID);
  EXPECT_EQ(snapshot.health.invalid_samples(), 1u);
  EXPECT_EQ(snapshot.packet->scalar().acquisition_sequence(), 5u);
}
TEST(ObservationStateTest, MetadataPreservesDeviceClockAndOriginalReceiptAcrossCachedReads) {
  auto t = ObservationState::Clock::time_point{} + 1s;
  ObservationState state(100ms, t);
  PerceptionPacket p;
  auto* scalar = p.mutable_scalar();
  scalar->set_boot_id(9);
  scalar->set_clock(1);
  scalar->set_unit(7);
  scalar->set_session_id(2);
  scalar->set_subscription_id(3);
  scalar->set_acquisition_sequence(4);
  scalar->set_acquisition_us(12345);
  scalar->set_usable(true);
  scalar->set_receipt_fresh(true);
  state.Record(p, t, 5000000000);
  const auto first = state.Get(t).packet->sample_metadata();
  EXPECT_EQ(first.acquisition_ticks(), 12345u);
  EXPECT_EQ(first.acquisition_tick_ns(), 1000u);
  EXPECT_EQ(first.acquisition_clock_id(), "jw-device:9:1");
  EXPECT_EQ(first.source_sequence(), 4u);
  EXPECT_TRUE(first.discontinuity());
  EXPECT_FALSE(first.source_instance_id().empty());
  state.Record(p, t + 50ms, 6000000000);
  auto cached = state.Get(t + 50ms).packet;
  EXPECT_EQ(cached->sample_metadata().SerializeAsString(), first.SerializeAsString());
  EXPECT_DOUBLE_EQ(cached->timestamp(), 0);  // Device acquisition remains unmapped.
  // A wall-clock step cannot alter steady-clock freshness.
  scalar->set_acquisition_sequence(5);
  state.Record(p, t + 60ms, 4000000000);
  auto next = state.Get(t + 70ms);
  EXPECT_FALSE(next.packet->sample_metadata().discontinuity());
  EXPECT_EQ(next.health.last_valid_age_ns(), 10000000u);
  EXPECT_EQ(next.packet->sample_metadata().sequence(), first.sequence() + 1);
  EXPECT_EQ(next.packet->sample_metadata().host_receipt_unix_ns(), 4000000000);
  PerceptionPacket replay;
  ASSERT_TRUE(replay.ParseFromString(next.packet->SerializeAsString()));
  EXPECT_EQ(replay.sample_metadata().SerializeAsString(),
            next.packet->sample_metadata().SerializeAsString());
  scalar->set_boot_id(10);
  state.Record(p, t + 80ms, 4100000000);
  EXPECT_TRUE(state.Get(t + 80ms).packet->sample_metadata().discontinuity());
}
TEST(ObservationStateTest, MissingSourceTimingStaysUnavailableAndRestartChangesIdentity) {
  auto t = ObservationState::Clock::time_point{} + 1s;
  ObservationState state(100ms, t, {"sts3215_tick", "base"});
  auto p = Position(2048);
  p.set_timestamp(999.25);  // Driver's RobotTime read/acquisition estimate.
  state.Record(p, t, 123456789);
  auto first = state.Get(t).packet;
  EXPECT_DOUBLE_EQ(first->timestamp(), 999.25);
  EXPECT_FALSE(first->sample_metadata().has_acquisition_ticks());
  EXPECT_EQ(first->sample_metadata().native_unit(), "sts3215_tick");
  EXPECT_EQ(first->sample_metadata().frame_id(), "base");
  EXPECT_EQ(first->sample_metadata().host_receipt_unix_ns(), 123456789);
  state.Record(absl::UnavailableError("lost sample"), t + 10ms);
  state.Record(p, t + 20ms, 133456789);
  EXPECT_TRUE(state.Get(t + 20ms).packet->sample_metadata().discontinuity());
  ObservationState restarted(100ms, t);
  restarted.Record(p, t, 123456789);
  EXPECT_NE(first->sample_metadata().source_instance_id(),
            restarted.Get(t).packet->sample_metadata().source_instance_id());
}

class BlockingSensor : public PerceptionInterface {
 public:
  absl::Status Init() override {
    return absl::OkStatus();
  }
  std::string GetId() override {
    return "blocking";
  }
  absl::Status Teardown() override {
    release.set_value();
    return absl::OkStatus();
  }
  absl::StatusOr<PerceptionPacket> GetData() override {
    if (++reads == 1) return Position(1);
    entered.set_value();
    released.wait();
    return absl::UnavailableError("stopped");
  }
  std::promise<void> entered, release;
  std::shared_future<void> released = release.get_future().share();
  unsigned reads = 0;
};
TEST(ObservationSourceTest, BlockingAcquisitionCannotBlockHealthSnapshotOrStopRequest) {
  auto sensor = std::make_unique<BlockingSensor>();
  auto entered = sensor->entered.get_future();
  auto holder = std::make_shared<std::unique_ptr<BlockingSensor>>(std::move(sensor));
  ObservationSource source(
      [holder]() -> absl::StatusOr<std::unique_ptr<PerceptionInterface>> {
        return std::unique_ptr<PerceptionInterface>(std::move(*holder));
      },
      1ms,
      10ms);
  ASSERT_EQ(entered.wait_for(1s), std::future_status::ready);
  const auto before = ObservationState::Clock::now();
  EXPECT_EQ(source.Get().health.valid_samples(), 1u);
  EXPECT_LT(ObservationState::Clock::now() - before, 100ms);
  source.Stop();  // Teardown cancels the test backend before worker join.
  EXPECT_EQ(source.Get().health.state(), ObservationStatus::STOPPED);
}
TEST(ObservationSourceTest, ShutdownRunsTeardownOnceAndReportsCleanupFailure) {
  struct FailingSensor : BlockingSensor {
    FailingSensor(std::atomic<int>& calls, std::atomic<bool>& cancelled)
        : calls(calls), cancelled(cancelled) {}
    absl::Status Teardown() override {
      ++calls;
      cancelled = joshua::CancellationRequested();
      BlockingSensor::Teardown().IgnoreError();
      return absl::UnavailableError("sensor teardown failed");
    }
    std::atomic<int>& calls;
    std::atomic<bool>& cancelled;
  };
  std::atomic<int> calls{0};
  std::atomic<bool> cancelled{true};
  EXPECT_EQ(joshua::RunGuarded(
                "observation cleanup test",
                [&] {
                  auto sensor = std::make_unique<FailingSensor>(calls, cancelled);
                  auto entered = sensor->entered.get_future();
                  auto holder = std::make_shared<std::unique_ptr<FailingSensor>>(std::move(sensor));
                  ObservationSource source(
                      [holder]() -> absl::StatusOr<std::unique_ptr<PerceptionInterface>> {
                        return std::unique_ptr<PerceptionInterface>(std::move(*holder));
                      },
                      1ms,
                      10ms);
                  EXPECT_EQ(entered.wait_for(1s), std::future_status::ready);
                  joshua::RequestShutdown();
                  source.Stop();
                  source.Stop();
                  return 0;
                }),
            1);
  EXPECT_EQ(calls, 1);
  EXPECT_FALSE(cancelled);
  // Other tests represent independent process lifetimes.
  EXPECT_EQ(joshua::RunGuarded("reset observation test runtime", [] { return 0; }), 0);
}
TEST(ObservationStateTest, MalformedImageCannotEstablishReadiness) {
  auto t = ObservationState::Clock::time_point{};
  ObservationState state(100ms, t);
  PerceptionPacket packet;
  auto* image = packet.mutable_image();
  image->set_width(4);
  image->set_height(4);
  image->set_channels(3);
  image->set_encoding("bgr8");
  image->set_data("short");
  state.Record(packet, t);
  EXPECT_EQ(state.Get(t).health.state(), ObservationStatus::ERROR);
  EXPECT_FALSE(state.Get(t).packet);
}
}  // namespace
}  // namespace robot::perception
