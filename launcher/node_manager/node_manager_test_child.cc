#include <signal.h>
#include <unistd.h>

#include <cstdlib>
#include <fstream>
#include <stdexcept>
#include <string>

#include "utils/joshua_runtime.h"

namespace {
volatile sig_atomic_t cleanup_requested = 0;
void RequestCleanup(int) {
  cleanup_requested = 1;
}
void ExitOnTerm(int) {
  _exit(0);
}
}  // namespace

JOSHUA_MAIN(argc, argv) {
  if (argc < 2) return 2;
  std::string mode = argv[1];
  // Stand in for the packaged wrapper in the configured startup rollback test.
  // Its arguments are <executable> <node_name> <node_id> <config_path>.
  if (mode == "data_subscriber" && argc == 5)
    mode = std::string(argv[3]) == "1" ? "ready" : "fail-init";
  if (mode == "fail-init") throw std::runtime_error("fixture initialization failed");
  if (mode == "closed-ready") {
    const char* fd = std::getenv("JOSHUA_READY_FD");
    if (fd) close(std::atoi(fd));
    for (;;) pause();
  }
  if (mode == "no-ready")
    for (;;) pause();
  if (mode == "fail-cleanup") signal(SIGINT, RequestCleanup);
  if (mode == "term-only" || mode == "stubborn" || mode == "descendant") {
    signal(SIGINT, SIG_IGN);
    signal(SIGTERM, mode == "term-only" ? ExitOnTerm : SIG_IGN);
  }
  if (mode == "descendant") {
    if (argc < 3) return 2;
    const pid_t child = fork();
    if (child < 0) return 3;
    if (child == 0)
      for (;;) pause();
    std::ofstream file(argv[2]);
    file << child << '\n';
    if (!file.good()) return 4;
  }
  joshua::ThrowIfError(joshua::NotifyReady());
  if (mode == "fail-cleanup") {
    while (!cleanup_requested) usleep(1000);
    joshua::RunCleanup("fixture cleanup", [] { throw std::runtime_error("fixture stop failed"); });
    return 0;  // The process guard must preserve the cleanup failure.
  }
  if (mode == "complete") return 0;
  if (mode == "fail-running") {
    usleep(100000);
    return 7;
  }
  for (;;) pause();
}
