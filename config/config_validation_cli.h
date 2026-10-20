#pragma once

#include <ostream>
#include <string>
#include <vector>

namespace config {

// Internal command boundary. Returns the process exit code and writes its
// single JSON report to output (or plain text for --help).
int RunConfigValidationCli(const std::vector<std::string>& args, std::ostream& output);

}  // namespace config
