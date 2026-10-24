#include "node_generator/process_supervisor.h"

#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <fstream>

#include "gtest/gtest.h"
#include "utils/joshua_runtime.h"
#include "utils/scope_exit.h"

namespace node_generator {
namespace {
using Clock = std::chrono::steady_clock;

class ProcessSupervisorTest : public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_EQ(joshua::RunGuarded("process test", [] { return 0; }), 0);
    const char* runfiles = std::getenv("TEST_SRCDIR");
    const char* workspace = std::getenv("TEST_WORKSPACE");
    ASSERT_NE(runfiles, nullptr);
    ASSERT_NE(workspace, nullptr);
    fixture_ = (std::filesystem::path(runfiles) / workspace / "node_generator/process_fixture").string();
  }
  ProcessSpec Spec(const std::string& mode) {
    ProcessSpec spec;
    spec.name = mode;
    spec.executable = fixture_;
    spec.arguments = {mode};
    return spec;
  }
  ShutdownPolicy FastShutdown() {
    return {std::chrono::milliseconds(100), std::chrono::milliseconds(300),
            std::chrono::milliseconds(1000)};
  }
  std::string fixture_;
};

TEST_F(ProcessSupervisorTest, TracksReadinessAndReapsOnlyItsOwnChildren) {
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
  ProcessSupervisor supervisor;
  ASSERT_TRUE(supervisor.Start(Spec("ready")).ok());
  ASSERT_TRUE(supervisor.AwaitReady(std::chrono::seconds(5)).ok());
  auto snapshots = supervisor.Snapshots();
  ASSERT_EQ(snapshots.size(), 1);
  EXPECT_GT(snapshots[0].pid, 0);
  EXPECT_NE(snapshots[0].pid, snapshots[0].guardian_pid);
  EXPECT_EQ(snapshots[0].state, ProcessState::kReady);
  EXPECT_TRUE(supervisor.Shutdown(FastShutdown()).ok());
  EXPECT_TRUE(supervisor.Shutdown(FastShutdown()).ok());
  EXPECT_TRUE(supervisor.Snapshots()[0].reaped);
  int status;
  EXPECT_EQ(waitpid(unrelated, &status, 0), unrelated);
  unrelated_reaped = true;
  EXPECT_EQ(WEXITSTATUS(status), 8);
}

TEST_F(ProcessSupervisorTest, FailedExecAndInitializationRemainObservable) {
  for (const auto& mode : {"missing-executable", "fail-init", "closed-ready"}) {
    ProcessSupervisor supervisor;
    auto spec = Spec(mode);
    if (spec.name == "missing-executable") spec.executable += ".missing";
    ASSERT_TRUE(supervisor.Start(spec).ok());
    EXPECT_FALSE(supervisor.AwaitReady(std::chrono::seconds(5)).ok());
    supervisor.Shutdown(FastShutdown()).IgnoreError();
    EXPECT_FALSE(supervisor.Snapshots()[0].error.empty());
    EXPECT_TRUE(supervisor.Snapshots()[0].reaped);
  }
}

TEST_F(ProcessSupervisorTest, UnexpectedExitRetainsApplicationStatus) {
  ProcessSupervisor supervisor;
  ASSERT_TRUE(supervisor.Start(Spec("fail-running")).ok());
  ASSERT_TRUE(supervisor.AwaitReady(std::chrono::seconds(5)).ok());
  absl::Status status;
  const auto deadline = Clock::now() + std::chrono::seconds(5);
  while (status.ok() && Clock::now() < deadline) {
    status = supervisor.Poll();
    poll(nullptr, 0, 10);
  }
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(supervisor.Snapshots()[0].exit_code, 7);
  EXPECT_FALSE(supervisor.Snapshots()[0].expected_exit);
  supervisor.Shutdown(FastShutdown()).IgnoreError();
  EXPECT_EQ(supervisor.Snapshots()[0].exit_code, 7);
}

TEST_F(ProcessSupervisorTest, PartialStartupFailureCanRollBackEveryStartedJob) {
  ProcessSupervisor supervisor;
  ASSERT_TRUE(supervisor.Start(Spec("ready")).ok());
  ASSERT_TRUE(supervisor.Start(Spec("fail-init")).ok());
  EXPECT_FALSE(supervisor.AwaitReady(std::chrono::seconds(5)).ok());
  supervisor.Shutdown(FastShutdown()).IgnoreError();
  ASSERT_EQ(supervisor.Snapshots().size(), 2);
  for (const auto& state : supervisor.Snapshots()) EXPECT_TRUE(state.reaped);
}

TEST_F(ProcessSupervisorTest, LiveProcessWithoutReadinessTimesOut) {
  ProcessSupervisor supervisor;
  ASSERT_TRUE(supervisor.Start(Spec("no-ready")).ok());
  const auto status = supervisor.AwaitReady(std::chrono::milliseconds(200));
  EXPECT_EQ(status.code(), absl::StatusCode::kDeadlineExceeded);
  EXPECT_FALSE(supervisor.Snapshots()[0].ready);
  supervisor.Shutdown(FastShutdown()).IgnoreError();
  EXPECT_TRUE(supervisor.Snapshots()[0].reaped);
}

TEST_F(ProcessSupervisorTest, FiniteJobCanCompleteIntentionally) {
  ProcessSupervisor supervisor;
  auto spec = Spec("complete");
  spec.allow_completion = true;
  ASSERT_TRUE(supervisor.Start(spec).ok());
  ASSERT_TRUE(supervisor.AwaitReady(std::chrono::seconds(5)).ok());
  const auto deadline = Clock::now() + std::chrono::seconds(5);
  while (supervisor.HasLiveProcesses() && Clock::now() < deadline) {
    ASSERT_TRUE(supervisor.Poll().ok());
    poll(nullptr, 0, 10);
  }
  EXPECT_FALSE(supervisor.HasLiveProcesses());
  EXPECT_TRUE(supervisor.Shutdown(FastShutdown()).ok());
  EXPECT_TRUE(supervisor.Snapshots()[0].expected_exit);
  EXPECT_EQ(supervisor.Snapshots()[0].exit_code, 0);
}

TEST_F(ProcessSupervisorTest, GivesSigtermItsOwnGracePeriod) {
  ProcessSupervisor supervisor;
  ASSERT_TRUE(supervisor.Start(Spec("term-only")).ok());
  ASSERT_TRUE(supervisor.AwaitReady(std::chrono::seconds(5)).ok());
  EXPECT_TRUE(supervisor.Shutdown(FastShutdown()).ok());
  const auto state = supervisor.Snapshots()[0];
  EXPECT_EQ(state.exit_code, 0);
  EXPECT_FALSE(state.forced_shutdown);
  EXPECT_TRUE(state.reaped);
}

TEST_F(ProcessSupervisorTest, KillsAndReapsSameGroupDescendants) {
  ProcessSupervisor supervisor;
  auto spec = Spec("descendant");
  const auto path = std::filesystem::path(std::getenv("TEST_TMPDIR")) / "descendant.pid";
  spec.arguments.push_back(path.string());
  ASSERT_TRUE(supervisor.Start(spec).ok());
  ASSERT_TRUE(supervisor.AwaitReady(std::chrono::seconds(5)).ok());
  pid_t descendant = -1;
  std::ifstream(path) >> descendant;
  ASSERT_GT(descendant, 0);
  EXPECT_TRUE(supervisor.Shutdown(FastShutdown()).ok());
  EXPECT_TRUE(supervisor.Snapshots()[0].forced_shutdown);
  EXPECT_TRUE(supervisor.Snapshots()[0].reaped);
  errno = 0;
  EXPECT_EQ(kill(descendant, 0), -1);
  EXPECT_EQ(errno, ESRCH);
}

TEST_F(ProcessSupervisorTest, ParentDeathKillsTheOwnedGroup) {
  // Keep adopted descendants reaped by this test process, rather than PID 1.
  ProcessSupervisor reaper;
  int channel[2];
  ASSERT_EQ(pipe(channel), 0);
  const pid_t owner = fork();
  ASSERT_GE(owner, 0);
  if (owner == 0) {
    close(channel[0]);
    ProcessSupervisor supervisor;
    if (!supervisor.Start(Spec("stubborn")).ok() ||
        !supervisor.AwaitReady(std::chrono::seconds(5)).ok()) _exit(2);
    const pid_t group = supervisor.Snapshots()[0].process_group;
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
