#include "robot/action/interfaces/control_gate.h"

#include <future>

#include "gtest/gtest.h"

namespace robot::action {
namespace {
using namespace std::chrono_literals;
class FakeAction : public ActionInterface {
 public:
  absl::Status Init() override {
    return absl::OkStatus();
  }
  std::string GetId() override {
    return "test";
  }
  absl::Status Teardown() override {
    ++teardowns;
    if (!teardown_status.ok()) return teardown_status;
    enabled = false;
    return absl::OkStatus();
  }
  absl::Status SetAction(const ActionPacket& p) override {
    if (p.has_joint()) {
      ++targets;
      return target_status;
    }
    if (p.preset() == PRESET_ENABLE_TORQUE) {
      enabled = true;
      ++enables;
    }
    if (p.preset() == PRESET_DISABLE_TORQUE) {
      if (!disable_status.ok()) return disable_status;
      enabled = false;
      ++disables;
    }
    return absl::OkStatus();
  }
  bool enabled = true;
  int targets = 0, enables = 0, disables = 0, teardowns = 0;
  absl::Status teardown_status;
  absl::Status target_status;
  absl::Status disable_status;
};
ActionPacket Target(uint64_t stamp = 0) {
  ActionPacket p;
  p.mutable_joint()->set_position(1);
  p.set_timestamp(joshua::RobotTimeFromUnixNanoseconds(stamp));
  return p;
}

TEST(ControlGateTest, DisableAfterExpiryStillRequiresRecovery) {
  auto action = std::make_shared<FakeAction>();
  ControlPolicy policy;
  policy.set_command_timeout_ms(100);
  ControlGate gate(action, policy, {"default", "backup"});
  auto t = ControlGate::Clock::time_point{};
  ASSERT_TRUE(gate.Initialize().ok());
  ASSERT_TRUE(gate.Enable(t).ok());
  ActionPacket disable;
  disable.set_preset(PRESET_DISABLE_TORQUE);
  ASSERT_TRUE(gate.Execute("backup", disable, 0, t + 101ms).ok());
  EXPECT_FALSE(action->enabled);
  EXPECT_FALSE(gate.Enable(t + 102ms).ok());
  ASSERT_TRUE(gate.Recover(0).ok());
  EXPECT_TRUE(gate.Enable(t + 103ms).ok());
}

TEST(ControlGateTest, DisabledStartupExclusiveSourceAndLatchedCommandExpiry) {
  auto action = std::make_shared<FakeAction>();
  ControlPolicy policy;
  policy.set_initial_source("manual");
  policy.set_command_timeout_ms(100);
  ControlGate gate(action, policy, {"manual", "policy"});
  auto t = ControlGate::Clock::time_point{};
  ASSERT_TRUE(gate.Initialize().ok());
  EXPECT_FALSE(action->enabled);
  EXPECT_FALSE(gate.Execute("manual", Target(), 0, t).ok());
  ASSERT_TRUE(gate.Enable(t).ok());
  EXPECT_FALSE(gate.SelectSource("policy", 0).ok());
  EXPECT_FALSE(gate.Execute("policy", Target(), 0, t).ok());
  ASSERT_TRUE(gate.Execute("manual", Target(), 0, t + 50ms).ok());
  ASSERT_TRUE(gate.Enable(t + 140ms).ok());  // Does not extend last target's lease.
  EXPECT_TRUE(gate.Poll(t + 150ms).ok());
  EXPECT_FALSE(action->enabled);
  EXPECT_EQ(gate.state(), ControlGate::State::kStopped);
  EXPECT_FALSE(gate.Enable(t + 151ms).ok());
  EXPECT_FALSE(gate.Execute("manual", Target(), 0, t + 151ms).ok());
  ASSERT_TRUE(gate.Recover(0).ok());
  EXPECT_FALSE(action->enabled);
  ASSERT_TRUE(gate.SelectSource("policy", 0).ok());
  ASSERT_TRUE(gate.Enable(t + 160ms).ok());
  EXPECT_FALSE(gate.Execute("manual", Target(), 0, t + 161ms).ok());
  EXPECT_TRUE(gate.Execute("policy", Target(), 0, t + 161ms).ok());
  EXPECT_EQ(action->targets, 2);
}
TEST(ControlGateTest, StandaloneShutdownOnlyParksAnEnabledActuatorWithALiveLease) {
  const auto now = ControlGate::Clock::now();
  for (const auto state : {ControlGate::State::kDisabled,
                           ControlGate::State::kStopped,
                           ControlGate::State::kFault,
                           ControlGate::State::kEnabled}) {
    SCOPED_TRACE(static_cast<int>(state));
    auto action = std::make_shared<FakeAction>();
    ControlGate gate(action, {}, {"default"});
    ASSERT_TRUE(gate.Initialize().ok());
    if (state != ControlGate::State::kDisabled)
      ASSERT_TRUE(gate.Enable(state == ControlGate::State::kEnabled ? now - 2s : now).ok());
    if (state == ControlGate::State::kStopped) gate.RequestStop();
    if (state == ControlGate::State::kFault) {
      action->target_status = absl::UnavailableError("write failed");
      ASSERT_FALSE(gate.Execute("default", Target(), 0, now).ok());
    }
    EXPECT_TRUE(gate.Shutdown(ControlGate::ShutdownMode::kParkIfEnabled).ok());
    EXPECT_EQ(action->teardowns, 0);
    EXPECT_FALSE(action->enabled);
  }
}

TEST(ControlGateTest, FailedParkingStillDisablesAndClosesOnce) {
  auto action = std::make_shared<FakeAction>();
  ControlGate gate(action, {}, {"default"});
  ASSERT_TRUE(gate.Initialize().ok());
  ASSERT_TRUE(gate.Enable(ControlGate::Clock::now()).ok());
  action->teardown_status = absl::UnavailableError("idle target failed");
  EXPECT_EQ(gate.Shutdown(ControlGate::ShutdownMode::kParkIfEnabled), action->teardown_status);
  EXPECT_EQ(gate.state(), ControlGate::State::kClosed);
  EXPECT_FALSE(action->enabled);
  EXPECT_TRUE(gate.Shutdown().ok());
  EXPECT_EQ(action->teardowns, 1);
}

TEST(ControlGateTest, SourceAgeDuplicateAndPriorGenerationStampsNeverRenewLease) {
  auto action = std::make_shared<FakeAction>();
  ControlPolicy policy;
  policy.set_max_input_age_ms(10);
  policy.set_command_timeout_ms(100);
  ControlGate gate(action, policy, {"default"});
  auto t = ControlGate::Clock::time_point{};
  ASSERT_TRUE(gate.Initialize().ok());
  ASSERT_TRUE(gate.Enable(t).ok());
  EXPECT_FALSE(gate.Execute("default", Target(), 20000000, t).ok());
  EXPECT_FALSE(gate.Execute("default", Target(1), 20000000, t).ok());
  EXPECT_FALSE(gate.Execute("default", Target(21000000), 20000000, t).ok());
  ASSERT_TRUE(gate.Execute("default", Target(15000000), 20000000, t + 1ms).ok());
  EXPECT_FALSE(gate.Execute("default", Target(15000000), 20000000, t + 90ms).ok());
  ASSERT_TRUE(gate.Poll(t + 101ms).ok());
  EXPECT_FALSE(action->enabled);
  ASSERT_TRUE(gate.Recover(25000000).ok());
  ASSERT_TRUE(gate.Enable(t + 110ms).ok());
  EXPECT_FALSE(gate.Execute("default", Target(24000000), 26000000, t + 111ms).ok());
  EXPECT_EQ(action->targets, 1);
}
TEST(ControlGateTest, StampedCommandsBeforeEnableCannotCrossTheEnableBoundary) {
  auto action = std::make_shared<FakeAction>();
  ControlPolicy policy;
  policy.set_max_input_age_ms(10);
  ControlGate gate(action, policy, {"default"});
  auto t = ControlGate::Clock::time_point{};
  ASSERT_TRUE(gate.Initialize().ok());
  ASSERT_TRUE(gate.Enable(t, 20000000).ok());
  EXPECT_FALSE(gate.Execute("default", Target(19000000), 20000000, t).ok());
  EXPECT_TRUE(gate.Execute("default", Target(20000000), 20000000, t).ok());
  EXPECT_EQ(action->targets, 1);
}

TEST(ControlGateTest, RobotTimeEpochAndNonfiniteStampsRespectRosFreshness) {
  auto action = std::make_shared<FakeAction>();
  ControlPolicy policy;
  policy.set_max_input_age_ms(10);
  ControlGate gate(action, policy, {"default"});
  const auto t = ControlGate::Clock::time_point{};
  const int64_t ros_epoch_ns = static_cast<int64_t>(joshua::kRobotStartTime) * 1000000000;
  ASSERT_TRUE(gate.Initialize().ok());
  const auto just_before_epoch = ros_epoch_ns - 2;
  ASSERT_TRUE(gate.Enable(t, just_before_epoch).ok());
  auto boundary = Target();
  boundary.set_timestamp(-2e-9);
  ASSERT_TRUE(gate.Execute("default", boundary, just_before_epoch, t).ok());
  auto packet = Target(ros_epoch_ns);
  ASSERT_DOUBLE_EQ(packet.timestamp(), 0.0);
  ASSERT_TRUE(gate.Execute("default", packet, ros_epoch_ns, t).ok());
  for (double invalid : {std::numeric_limits<double>::quiet_NaN(),
                         std::numeric_limits<double>::infinity(),
                         -std::numeric_limits<double>::infinity(),
                         std::numeric_limits<double>::max()}) {
    packet.set_timestamp(invalid);
    EXPECT_FALSE(gate.Execute("default", packet, ros_epoch_ns, t).ok());
  }
  EXPECT_TRUE(
      gate.Execute("default", Target(ros_epoch_ns + 5000000), ros_epoch_ns + 5000000, t + 5ms)
          .ok());
  EXPECT_EQ(action->targets, 3);
}

TEST(ControlGateTest, DriverFailureStopsAndRequiresExplicitRecoveryWithoutReplay) {
  auto action = std::make_shared<FakeAction>();
  ControlGate gate(action, {}, {"default"});
  auto t = ControlGate::Clock::time_point{};
  ASSERT_TRUE(gate.Initialize().ok());
  ASSERT_TRUE(gate.Enable(t).ok());
  action->target_status = absl::UnavailableError("Unknown output outcome");
  EXPECT_FALSE(gate.Execute("default", Target(), 0, t).ok());
  EXPECT_FALSE(action->enabled);
  EXPECT_EQ(gate.state(), ControlGate::State::kFault);
  EXPECT_FALSE(gate.Enable(t).ok());
  EXPECT_EQ(action->targets, 1);
  ASSERT_TRUE(gate.Recover(0).ok());
  EXPECT_FALSE(action->enabled);
  EXPECT_EQ(action->enables, 1);
  ASSERT_TRUE(gate.Shutdown().ok());
  EXPECT_FALSE(gate.Recover(0).ok());
  EXPECT_FALSE(gate.Enable(t).ok());
}

class BlockingAction : public FakeAction {
 public:
  absl::Status SetAction(const ActionPacket& packet) override {
    if (packet.has_joint()) {
      entered.set_value();
      released.wait();
    }
    return FakeAction::SetAction(packet);
  }
  std::promise<void> entered, release;
  std::shared_future<void> released = release.get_future().share();
};

TEST(ControlGateTest, StopAdmissionBypassesBlockedIoAndRejectsQueuedMotionUntilRecovery) {
  auto action = std::make_shared<BlockingAction>();
  ControlGate gate(action, {}, {"default"});
  auto t = ControlGate::Clock::now();
  ASSERT_TRUE(gate.Initialize().ok());
  ASSERT_TRUE(gate.Enable(t).ok());
  auto target =
      std::async(std::launch::async, [&] { return gate.Execute("default", Target(), 0, t); });
  action->entered.get_future().wait();
  auto interlocked = std::async(std::launch::async, [&] { return gate.RequestStop(); });
  EXPECT_EQ(interlocked.wait_for(100ms), std::future_status::ready);
  action->release.set_value();
  const auto request = interlocked.get();
  EXPECT_TRUE(target.get().ok());
  EXPECT_EQ(gate.state(), ControlGate::State::kStopped);
  EXPECT_FALSE(gate.Execute("default", Target(), 0, t).ok());
  EXPECT_FALSE(gate.Enable(t).ok());
  EXPECT_EQ(action->targets, 1);
  EXPECT_FALSE(gate.Recover(0).ok());
  const int disables = action->disables;
  ASSERT_TRUE(gate.Stop(true).ok());  // Independent board stop already disabled outputs.
  EXPECT_EQ(action->disables, disables);
  EXPECT_FALSE(gate.Enable(t).ok());
  EXPECT_FALSE(gate.Recover(0).ok());
  ASSERT_TRUE(gate.CompleteStopRequest(request).ok());
  ASSERT_TRUE(gate.Recover(0).ok());
  EXPECT_FALSE(action->enabled);
  ASSERT_TRUE(gate.Enable(t).ok());
}

TEST(ControlGateTest, ANewStopCannotBeClearedByEarlierRecovery) {
  auto action = std::make_shared<FakeAction>();
  ControlGate gate(action, {}, {"default"});
  ASSERT_TRUE(gate.Initialize().ok());
  ASSERT_TRUE(gate.Stop(true).ok());
  const auto request = gate.RequestStop();
  EXPECT_FALSE(gate.Recover(0).ok());
  EXPECT_FALSE(gate.SelectSource("default", 0).ok());
  ASSERT_TRUE(gate.Stop(true).ok());
  EXPECT_FALSE(gate.Recover(0).ok());
  ASSERT_TRUE(gate.CompleteStopRequest(request).ok());
  ASSERT_TRUE(gate.Recover(0).ok());
  EXPECT_EQ(gate.state(), ControlGate::State::kDisabled);
}

TEST(ControlGateTest, LocalStopCannotCompletePendingBusStop) {
  auto action = std::make_shared<FakeAction>();
  ControlGate gate(action, {}, {"default"});
  ASSERT_TRUE(gate.Initialize().ok());
  ASSERT_TRUE(gate.Enable(ControlGate::Clock::now()).ok());
  const auto bus_stop = gate.RequestStop();
  ASSERT_TRUE(gate.Stop().ok());
  EXPECT_FALSE(gate.Recover(0).ok());
  EXPECT_FALSE(gate.Enable(ControlGate::Clock::now()).ok());
  EXPECT_FALSE(action->enabled);
  ASSERT_TRUE(gate.CompleteStopRequest(bus_stop).ok());
  EXPECT_FALSE(gate.Enable(ControlGate::Clock::now()).ok());
  ASSERT_TRUE(gate.Recover(0).ok());
  EXPECT_TRUE(gate.Enable(ControlGate::Clock::now()).ok());
}

TEST(ControlGateTest, OutOfOrderCompletionDoesNotClearOtherRequests) {
  auto action = std::make_shared<FakeAction>();
  ControlGate gate(action, {}, {"default"});
  ASSERT_TRUE(gate.Initialize().ok());
  const auto first = gate.RequestStop();
  const auto second = gate.RequestStop();
  ASSERT_TRUE(gate.Stop(true).ok());
  ASSERT_TRUE(gate.CompleteStopRequest(second).ok());
  EXPECT_FALSE(gate.CompleteStopRequest(second).ok());
  EXPECT_FALSE(gate.CompleteStopRequest(second + 100).ok());
  EXPECT_FALSE(gate.Recover(0).ok());
  ASSERT_TRUE(gate.CompleteStopRequest(first).ok());
  ASSERT_TRUE(gate.Recover(0).ok());
  EXPECT_TRUE(gate.Enable(ControlGate::Clock::now()).ok());
}

TEST(ControlGateTest, CompletingBusRequestPreservesFailedDisableFault) {
  auto action = std::make_shared<FakeAction>();
  ControlGate gate(action, {}, {"default"});
  ASSERT_TRUE(gate.Initialize().ok());
  ASSERT_TRUE(gate.Enable(ControlGate::Clock::now()).ok());
  const auto bus_stop = gate.RequestStop();
  action->disable_status = absl::UnavailableError("Disable acknowledgment lost");
  EXPECT_FALSE(gate.Stop().ok());
  ASSERT_TRUE(gate.CompleteStopRequest(bus_stop).ok());
  EXPECT_EQ(gate.state(), ControlGate::State::kFault);
  EXPECT_FALSE(gate.Enable(ControlGate::Clock::now()).ok());
  EXPECT_FALSE(gate.Recover(0).ok());
  action->disable_status = absl::OkStatus();
  EXPECT_TRUE(gate.Recover(0).ok());
  EXPECT_FALSE(action->enabled);
}
}  // namespace
}  // namespace robot::action
