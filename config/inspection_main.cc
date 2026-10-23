#include <glog/logging.h>

#include <iostream>

#include "config/inspection_cli.h"
int main(int argc, char** argv) {
  google::InitGoogleLogging(argv[0]);
  FLAGS_logtostderr = 1;
  return config::RunInspectionCli({argv + 1, argv + argc}, std::cout);
}
