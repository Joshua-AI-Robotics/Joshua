#include <iostream>
#include <string>

#include "config/config_utils.h"
#include "config/ros2_network.h"
#include "gflags/gflags.h"
#include "glog/logging.h"

DEFINE_string(config, "", "Preset to read (network settings only; never launches nodes)");
DEFINE_string(output_parent, "", "Existing directory for persistent exported profiles");

namespace {
std::string ShellQuote(const std::string& value) {
  std::string result = "'";
  for (char c : value) result += c == '\'' ? "'\\''" : std::string(1, c);
  return result + "'";
}
}  // namespace

int main(int argc, char** argv) {
  google::InitGoogleLogging(argv[0]);
  FLAGS_logtostderr = 1;
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  if (FLAGS_config.empty()) {
    LOG(ERROR) << "Specify --config=<preset>";
    return 1;
  }
  const auto loaded = config::config_util::LoadConfig(FLAGS_config);
  if (!loaded.ok()) return 1;
  auto prepared = config::Ros2NetworkEnvironment::Prepare(
      loaded->general().ros2_network(), FLAGS_output_parent, false);
  if (!prepared.ok()) {
    LOG(ERROR) << prepared.status();
    return 1;
  }
  for (const auto& [key, value] : (*prepared)->variables()) {
    std::cout << "export " << key << '=' << ShellQuote(value) << '\n';
  }
  if (!std::cout.good()) return 1;
  (*prepared)->KeepFiles();
  return 0;
}
