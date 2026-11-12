#pragma once
#include <ostream>
#include <string>
#include <vector>
namespace config {
// Pure read-only command handler, independently callable without main/ROS.
int RunInspectionCli(const std::vector<std::string>& args, std::ostream& output);
}  // namespace config
