#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "absl/status/statusor.h"
#include "config/proto/config.pb.h"

namespace node_generator {
struct ProcessFact {
  int pid;
  uint32_t node_id;
  std::string node_name;
  std::string node_type;
};

// Passive adapter owned by NodeGenerator. Prepare is ROS-free and must precede
// forks; Start is called only after all forks. Failures never stop children.
class RuntimeObservation {
 public:
  static absl::StatusOr<std::unique_ptr<RuntimeObservation>> Prepare(const config::Config& config,
                                                                     const std::string& source,
                                                                     const std::string& directory,
                                                                     const std::string& sensor);
  ~RuntimeObservation();
  void Start() noexcept;
  void RecordExit(const ProcessFact& process, int wait_status) noexcept;
  void Tick(const std::vector<ProcessFact>& processes,
            size_t expected,
            const std::string& phase) noexcept;

 private:
  struct Impl;
  explicit RuntimeObservation(std::unique_ptr<Impl> impl);
  std::unique_ptr<Impl> impl_;
};
}  // namespace node_generator
