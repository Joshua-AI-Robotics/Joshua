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
