#include <signal.h>

#include <iostream>
#include <string>
#include <vector>

#include "config/config_validation_cli.h"
#include "glog/logging.h"

int main(int argc, char** argv) {
  signal(SIGPIPE, SIG_IGN);
  FLAGS_logtostderr = 1;
  google::InitGoogleLogging(argv[0]);
  std::vector<std::string> args;
  for (int i = 1; i < argc; ++i) args.emplace_back(argv[i]);
  return config::RunConfigValidationCli(args, std::cout);
}
