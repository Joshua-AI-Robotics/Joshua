#include <gtest/gtest.h>
#include <signal.h>
#include <unistd.h>

#include "node_generator/node_generator.h"

namespace node_generator {

class NodeGeneratorMonitorTest : public testing::Test {
 protected:
  static void Run(bool fail_fast) {
    alarm(10);  // Bound a regression that leaves the monitor waiting forever.
    NodeGenerator generator("unused.pbtxt");
    generator.SetupSignalHandlers();
    const pid_t parent = getpid();
    for (int index = 0; index < 2; ++index) {
      const pid_t pid = fork();
      if (pid == 0) {
        setpgid(0, 0);
        signal(SIGINT, SIG_DFL);
        signal(SIGTERM, SIG_DFL);
        if (index == 0) _exit(7);
        if (!fail_fast) {
          sleep(2);
          kill(parent, SIGINT);
        }
        for (;;) pause();
      }
      if (pid < 0) _exit(99);
      setpgid(pid, pid);
      generator.launched_nodes_[pid] = NodeInfo{};
      generator.launched_nodes_[pid].node_name = "synthetic_child";
    }
    const auto status = generator.MonitorNodes(fail_fast);
    _exit(status.ok() ? 0 : 2);
  }
};

TEST_F(NodeGeneratorMonitorTest, FailedChildStopsRemainingGraphForSupervisor) {
  EXPECT_EXIT(Run(true), testing::ExitedWithCode(1), "supervisor restart");
}

TEST_F(NodeGeneratorMonitorTest, DefaultKeepsOtherNodesUntilShutdownSignal) {
  EXPECT_EXIT(Run(false), testing::ExitedWithCode(0), "Shutdown requested");
}

}  // namespace node_generator
