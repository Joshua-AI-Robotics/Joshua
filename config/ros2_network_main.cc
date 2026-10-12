#include <iostream>
#include <iterator>
#include <string>

#include "config/config_utils.h"
#include "config/ros2_network.h"
#include "gflags/gflags.h"
#include "glog/logging.h"

DEFINE_string(config, "", "Preset to read (network settings only; never launches nodes)");
DEFINE_string(output_parent, "", "Existing directory for persistent exported profiles");
DEFINE_bool(network_stdin, false, "Read serialized Ros2NetworkConfig from stdin (Python adapter)");
DEFINE_bool(null_output, false, "Emit NUL-delimited key=value entries instead of shell exports");

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
  config::Ros2NetworkConfig network;
  if (FLAGS_network_stdin && FLAGS_config.empty()) {
    const std::string bytes{std::istreambuf_iterator<char>(std::cin), {}};
    if (!network.ParseFromString(bytes)) return 1;
  } else if (!FLAGS_network_stdin && !FLAGS_config.empty()) {
    const auto loaded = config::config_util::LoadConfig(FLAGS_config);
    if (!loaded.ok()) return 1;
    network = loaded->general().ros2_network();
  } else {
    LOG(ERROR) << "Specify --config=<preset> or --network_stdin";
    return 1;
  }
  auto prepared =
      config::Ros2NetworkEnvironment::Prepare(network, FLAGS_output_parent, FLAGS_network_stdin);
  if (!prepared.ok()) {
    LOG(ERROR) << prepared.status();
    return 1;
  }
  for (const auto& [key, value] : (*prepared)->variables()) {
    if (FLAGS_null_output)
      std::cout << key << '=' << value << '\0';
    else
      std::cout << "export " << key << '=' << ShellQuote(value) << '\n';
  }
  if (!std::cout.good()) return 1;
  (*prepared)->KeepFiles();
  return 0;
}
