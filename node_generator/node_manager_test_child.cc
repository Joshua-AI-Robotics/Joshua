#include <signal.h>
#include <unistd.h>

#include <cstdlib>
#include <fstream>
#include <stdexcept>
#include <string>

#include "utils/joshua_runtime.h"

namespace {
void ExitOnTerm(int) { _exit(0); }
}  // namespace

JOSHUA_MAIN(argc, argv) {
  if (argc < 2) return 2;
  const std::string mode = argv[1];
  if (mode == "fail-init") throw std::runtime_error("fixture initialization failed");
  if (mode == "closed-ready") {
    const char* fd = std::getenv("JOSHUA_READY_FD");
    if (fd) close(std::atoi(fd));
    for (;;) pause();
  }
  if (mode == "no-ready") for (;;) pause();
  if (mode == "term-only" || mode == "stubborn" || mode == "descendant") {
    signal(SIGINT, SIG_IGN);
    signal(SIGTERM, mode == "term-only" ? ExitOnTerm : SIG_IGN);
  }
  if (mode == "descendant") {
    if (argc < 3) return 2;
    const pid_t child = fork();
    if (child < 0) return 3;
    if (child == 0) for (;;) pause();
    std::ofstream file(argv[2]);
    file << child << '\n';
    if (!file.good()) return 4;
  }
  joshua::ThrowIfError(joshua::NotifyReady());
  if (mode == "complete") return 0;
  if (mode == "fail-running") {
    usleep(100000);
    return 7;
  }
  for (;;) pause();
}
