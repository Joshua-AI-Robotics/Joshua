#include "node_generator/node_manager.h"

#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>

#include "gtest/gtest.h"
#include "utils/joshua_runtime.h"
#include "utils/scope_exit.h"

namespace node_generator {
namespace {
using Clock = std::chrono::steady_clock;

class NodeManagerTest : public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_EQ(joshua::RunGuarded("process test", [] { return 0; }), 0);
    const char* runfiles = std::getenv("TEST_SRCDIR");
    const char* workspace = std::getenv("TEST_WORKSPACE");
    ASSERT_NE(runfiles, nullptr);
    ASSERT_NE(workspace, nullptr);
    fixture_ = (std::filesystem::path(runfiles) / workspace /
                "node_generator/node_manager_test_child").string();
  }
  NodeManager::LaunchSpec Spec(const std::string& mode) {
    NodeManager::LaunchSpec spec;
    spec.name = mode;
    spec.executable = fixture_;
    spec.arguments = {mode};
    return spec;
  }
  NodeManager::ShutdownPolicy FastShutdown() {
    return {std::chrono::milliseconds(100), std::chrono::milliseconds(300),
            std::chrono::milliseconds(1000)};
  }
  std::string fixture_;
};

TEST_F(NodeManagerTest, TracksReadinessAndReapsOnlyItsOwnChildren) {
  const pid_t unrelated = fork();
  ASSERT_GE(unrelated, 0);
  if (unrelated == 0) _exit(8);
  bool unrelated_reaped = false;
  joshua::ScopeExit reap_unrelated([&] {
    if (!unrelated_reaped) {
      int status;
      waitpid(unrelated, &status, 0);
    }
  });
  NodeManager manager;
  auto spec = Spec("ready");
  spec.node_id = 42;
  spec.node_type = "position_publisher";
  spec.publish_topics = {"joint/position"};
  ASSERT_TRUE(manager.StartProcess(spec).ok());
  ASSERT_TRUE(manager.AwaitReady(std::chrono::seconds(5)).ok());
  auto snapshots = manager.GetNodeStatus();
  ASSERT_EQ(snapshots.size(), 1);
  EXPECT_GT(snapshots[0].pid, 0);
  EXPECT_NE(snapshots[0].pid, snapshots[0].guardian_pid);
  EXPECT_EQ(snapshots[0].state, NodeManager::State::kReady);
  EXPECT_EQ(snapshots[0].node_id, 42);
  EXPECT_EQ(snapshots[0].node_type, "position_publisher");
  EXPECT_EQ(snapshots[0].publish_topics, spec.publish_topics);
  EXPECT_TRUE(manager.Shutdown(FastShutdown()).ok());
  EXPECT_TRUE(manager.Shutdown(FastShutdown()).ok());
  EXPECT_TRUE(manager.GetNodeStatus()[0].reaped);
  int status;
  EXPECT_EQ(waitpid(unrelated, &status, 0), unrelated);
  unrelated_reaped = true;
  EXPECT_EQ(WEXITSTATUS(status), 8);
}

TEST_F(NodeManagerTest, OneManagerOwnsTheLauncherProcess) {
  NodeManager manager;
  EXPECT_THROW({
    NodeManager duplicate;
  }, std::logic_error);
}

TEST_F(NodeManagerTest, OwnsAndRestoresSignalHandlers) {
  struct sigaction before{}, after{};
  ASSERT_EQ(sigaction(SIGINT, nullptr, &before), 0);
  {
    NodeManager manager;
    EXPECT_FALSE(manager.ShutdownRequested());
    ASSERT_EQ(raise(SIGINT), 0);
    EXPECT_TRUE(manager.ShutdownRequested());
  }
  ASSERT_EQ(sigaction(SIGINT, nullptr, &after), 0);
  EXPECT_EQ(after.sa_handler, before.sa_handler);
}

TEST_F(NodeManagerTest, CompletionOfOneJobStopsTheOtherOwnedJobs) {
  NodeManager manager;
  ASSERT_TRUE(manager.StartProcess(Spec("ready")).ok());
  auto complete = Spec("complete");
  complete.allow_completion = true;
  ASSERT_TRUE(manager.StartProcess(complete).ok());
  ASSERT_TRUE(manager.AwaitReady(std::chrono::seconds(5)).ok());
  EXPECT_TRUE(manager.MonitorNodes("complete").ok());
  EXPECT_FALSE(manager.HasLiveNodes());
  for (const auto& state : manager.GetNodeStatus()) EXPECT_TRUE(state.reaped);
}

TEST_F(NodeManagerTest, FailedExecAndInitializationRemainObservable) {
  for (const auto& mode : {"missing-executable", "fail-init", "closed-ready"}) {
    NodeManager manager;
    auto spec = Spec(mode);
    if (spec.name == "missing-executable") spec.executable += ".missing";
    ASSERT_TRUE(manager.StartProcess(spec).ok());
    EXPECT_FALSE(manager.AwaitReady(std::chrono::seconds(5)).ok());
    manager.Shutdown(FastShutdown()).IgnoreError();
    EXPECT_FALSE(manager.GetNodeStatus()[0].error.empty());
    EXPECT_TRUE(manager.GetNodeStatus()[0].reaped);
  }
}

TEST_F(NodeManagerTest, UnexpectedExitRetainsApplicationStatus) {
  NodeManager manager;
  ASSERT_TRUE(manager.StartProcess(Spec("fail-running")).ok());
  ASSERT_TRUE(manager.AwaitReady(std::chrono::seconds(5)).ok());
  absl::Status status;
  const auto deadline = Clock::now() + std::chrono::seconds(5);
  while (status.ok() && Clock::now() < deadline) {
    status = manager.PollNodes();
    poll(nullptr, 0, 10);
  }
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(manager.GetNodeStatus()[0].exit_code, 7);
  EXPECT_FALSE(manager.GetNodeStatus()[0].expected_exit);
  manager.Shutdown(FastShutdown()).IgnoreError();
  EXPECT_EQ(manager.GetNodeStatus()[0].exit_code, 7);
}

TEST_F(NodeManagerTest, PartialStartupFailureCanRollBackEveryStartedJob) {
  NodeManager manager;
  ASSERT_TRUE(manager.StartProcess(Spec("ready")).ok());
  ASSERT_TRUE(manager.StartProcess(Spec("fail-init")).ok());
  EXPECT_FALSE(manager.AwaitReady(std::chrono::seconds(5)).ok());
  manager.Shutdown(FastShutdown()).IgnoreError();
  ASSERT_EQ(manager.GetNodeStatus().size(), 2);
  for (const auto& state : manager.GetNodeStatus()) EXPECT_TRUE(state.reaped);
}

TEST_F(NodeManagerTest, LiveProcessWithoutReadinessTimesOut) {
  NodeManager manager;
  ASSERT_TRUE(manager.StartProcess(Spec("no-ready")).ok());
  const auto status = manager.AwaitReady(std::chrono::milliseconds(200));
  EXPECT_EQ(status.code(), absl::StatusCode::kDeadlineExceeded);
  EXPECT_FALSE(manager.GetNodeStatus()[0].ready);
  manager.Shutdown(FastShutdown()).IgnoreError();
  EXPECT_TRUE(manager.GetNodeStatus()[0].reaped);
}

TEST_F(NodeManagerTest, FiniteJobCanCompleteIntentionally) {
  NodeManager manager;
  auto spec = Spec("complete");
  spec.allow_completion = true;
  ASSERT_TRUE(manager.StartProcess(spec).ok());
  ASSERT_TRUE(manager.AwaitReady(std::chrono::seconds(5)).ok());
  const auto deadline = Clock::now() + std::chrono::seconds(5);
  while (manager.HasLiveNodes() && Clock::now() < deadline) {
    ASSERT_TRUE(manager.PollNodes().ok());
    poll(nullptr, 0, 10);
  }
  EXPECT_FALSE(manager.HasLiveNodes());
  EXPECT_TRUE(manager.Shutdown(FastShutdown()).ok());
  EXPECT_TRUE(manager.GetNodeStatus()[0].expected_exit);
  EXPECT_EQ(manager.GetNodeStatus()[0].exit_code, 0);
}

TEST_F(NodeManagerTest, GivesSigtermItsOwnGracePeriod) {
  NodeManager manager;
  ASSERT_TRUE(manager.StartProcess(Spec("term-only")).ok());
  ASSERT_TRUE(manager.AwaitReady(std::chrono::seconds(5)).ok());
  EXPECT_TRUE(manager.Shutdown(FastShutdown()).ok());
  const auto state = manager.GetNodeStatus()[0];
  EXPECT_EQ(state.exit_code, 0);
  EXPECT_FALSE(state.forced_shutdown);
  EXPECT_TRUE(state.reaped);
}

TEST_F(NodeManagerTest, KillsAndReapsSameGroupDescendants) {
  NodeManager manager;
  auto spec = Spec("descendant");
  const auto path = std::filesystem::path(std::getenv("TEST_TMPDIR")) / "descendant.pid";
  spec.arguments.push_back(path.string());
  ASSERT_TRUE(manager.StartProcess(spec).ok());
  ASSERT_TRUE(manager.AwaitReady(std::chrono::seconds(5)).ok());
  pid_t descendant = -1;
  std::ifstream(path) >> descendant;
  ASSERT_GT(descendant, 0);
  EXPECT_TRUE(manager.Shutdown(FastShutdown()).ok());
  EXPECT_TRUE(manager.GetNodeStatus()[0].forced_shutdown);
  EXPECT_TRUE(manager.GetNodeStatus()[0].reaped);
  errno = 0;
  EXPECT_EQ(kill(descendant, 0), -1);
  EXPECT_EQ(errno, ESRCH);
}

TEST_F(NodeManagerTest, ParentDeathKillsTheOwnedGroup) {
  // Keep adopted descendants reaped by this test process, rather than PID 1.
  NodeManager reaper;
  int channel[2];
  ASSERT_EQ(pipe(channel), 0);
  const pid_t owner = fork();
  ASSERT_GE(owner, 0);
  if (owner == 0) {
    close(channel[0]);
    NodeManager manager;
    if (!manager.StartProcess(Spec("stubborn")).ok() ||
        !manager.AwaitReady(std::chrono::seconds(5)).ok()) _exit(2);
    const pid_t group = manager.GetNodeStatus()[0].process_group;
    if (write(channel[1], &group, sizeof(group)) != sizeof(group)) _exit(3);
    for (;;) pause();
  }
  close(channel[1]);
  bool owner_reaped = false;
  bool group_reaped = false;
  pid_t group = -1;
  joshua::ScopeExit cleanup([&] {
    if (!owner_reaped) {
      kill(owner, SIGKILL);
      int status;
      waitpid(owner, &status, 0);
    }
    if (group > 0 && !group_reaped) {
      kill(-group, SIGKILL);
      const auto deadline = Clock::now() + std::chrono::seconds(1);
      int status;
      while (Clock::now() < deadline) {
        while (waitpid(-group, &status, WNOHANG) > 0) {}
        if (kill(-group, 0) < 0 && errno == ESRCH) break;
        poll(nullptr, 0, 10);
      }
    }
    close(channel[0]);
  });
  pollfd ready{channel[0], POLLIN, 0};
  ASSERT_GT(poll(&ready, 1, 6000), 0);
  ASSERT_EQ(read(channel[0], &group, sizeof(group)), sizeof(group));
  ASSERT_GT(group, 0);
  ASSERT_EQ(kill(owner, SIGKILL), 0);
  int status;
  ASSERT_EQ(waitpid(owner, &status, 0), owner);
  owner_reaped = true;
  const auto deadline = Clock::now() + std::chrono::seconds(5);
  while (Clock::now() < deadline) {
    while (waitpid(-group, &status, WNOHANG) > 0) {}
    if (kill(-group, 0) < 0 && errno == ESRCH) {
      group_reaped = true;
      break;
    }
    poll(nullptr, 0, 10);
  }
  errno = 0;
  EXPECT_EQ(kill(-group, 0), -1);
  EXPECT_EQ(errno, ESRCH);
}

}  // namespace
}  // namespace node_generator
