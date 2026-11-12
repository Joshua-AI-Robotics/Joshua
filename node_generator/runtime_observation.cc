#include "node_generator/runtime_observation.h"

#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <deque>
#include <fstream>
#include <set>
#include <stdexcept>
#include <utility>

#include "google/protobuf/struct.pb.h"
#include "google/protobuf/util/json_util.h"
#include "rcl_interfaces/msg/log.hpp"
#include "rclcpp/rclcpp.hpp"
#include "sensor_msgs/msg/joint_state.hpp"
#include "version.h"

namespace node_generator {
namespace {
using google::protobuf::Value;
Value Object() {
  Value v;
  v.mutable_struct_value();
  return v;
}
Value Array() {
  Value v;
  v.mutable_list_value();
  return v;
}
Value Text(const std::string& s) {
  Value v;
  v.set_string_value(s);
  return v;
}
Value Number(double n) {
  Value v;
  v.set_number_value(n);
  return v;
}
Value Null() {
  Value v;
  v.set_null_value(google::protobuf::NULL_VALUE);
  return v;
}
void Set(Value& v, const std::string& key, Value x) {
  (*v.mutable_struct_value()->mutable_fields())[key] = std::move(x);
}
uint64_t Now() {
  timespec t{};
  if (clock_gettime(CLOCK_BOOTTIME, &t)) throw std::runtime_error("CLOCK_BOOTTIME unavailable");
  return static_cast<uint64_t>(t.tv_sec) * 1000000000 + t.tv_nsec;
}
std::string ReadIdentity(const char* path) {
  std::ifstream f(path);
  std::string line;
  std::getline(f, line);
  if (!f || line.empty() || line.size() > 128)
    throw std::runtime_error("Linux identity unavailable");
  return line;
}
std::string TimeNamespace() {
  char buf[128];
  auto n = readlink("/proc/self/ns/time", buf, sizeof(buf));
  if (n <= 0 || n == sizeof(buf)) throw std::runtime_error("Time namespace unavailable");
  return std::string(buf, n);
}
// Preserve complete valid UTF-8 code points. Invalid bytes/control characters
// are replaced, and callers count truncation or sanitization in log metadata.
std::string Bounded(const std::string& raw, size_t cap, bool* altered = nullptr) {
  std::string text;
  size_t i = 0;
  while (i < raw.size() && i < cap) {
    const unsigned char c = raw[i];
    size_t n = c < 128                  ? 1
               : c >= 0xc2 && c <= 0xdf ? 2
               : c >= 0xe0 && c <= 0xef ? 3
               : c >= 0xf0 && c <= 0xf4 ? 4
                                        : 0;
    bool valid = n && i + n <= raw.size();
    for (size_t j = 1; valid && j < n; ++j)
      valid = (static_cast<unsigned char>(raw[i + j]) & 0xc0) == 0x80;
    if (valid && n > 1) {
      const unsigned char second = raw[i + 1];
      valid = !((c == 0xe0 && second < 0xa0) || (c == 0xed && second >= 0xa0) ||
                (c == 0xf0 && second < 0x90) || (c == 0xf4 && second >= 0x90));
    }
    if (valid && i + n > cap) break;
    if (!valid || c < 32 || c == 127) {
      text += '?';
      ++i;
    } else {
      text.append(raw, i, n);
      i += n;
    }
  }
  if (altered) *altered = text != raw;
  return text;
}
std::string NodeName(const ros2::node::Node& node) {
  std::string name = ros2::node::NodeType_Name(node.node_type());
  std::transform(
      name.begin(), name.end(), name.begin(), [](unsigned char c) { return std::tolower(c); });
  return name + "_node_" + std::to_string(node.id());
}
std::string Gid(const uint8_t* bytes, size_t length) {
  static const char hex[] = "0123456789abcdef";
  std::string s;
  for (size_t i = 0; i < length; ++i) {
    s += hex[bytes[i] >> 4];
    s += hex[bytes[i] & 15];
  }
  return s;
}
Value Process(const ProcessFact& p) {
  Value v = Object();
  Set(v, "pid", Number(p.pid));
  Set(v, "node_id", Number(p.node_id));
  Set(v, "node_name", Text(Bounded(p.node_name, 128)));
  Set(v, "node_type", Text(Bounded(p.node_type, 128)));
  return v;
}
bool PrivateRegular(int fd) {
  struct stat s {};
  return fstat(fd, &s) == 0 && S_ISREG(s.st_mode) && s.st_uid == geteuid() && !(s.st_mode & 077) &&
         s.st_nlink == 1;
}
class SnapshotWriter {
 public:
  SnapshotWriter(const std::string& directory, const std::string& instance)
      : temporary_(".snapshot." + instance) {
    directory_ = open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (directory_ < 0)
      throw std::runtime_error("Observation directory must exist and not be a symlink.");
    try {
      struct stat s {};
      if (fstat(directory_, &s) || s.st_uid != geteuid() || (s.st_mode & 077))
        throw std::runtime_error("Observation directory must be private and owned by this UID.");
      // Pin and inspect existing lease objects before opening for write; no
      // character device/FIFO can be opened as a side effect of rejection.
      int pin = openat(directory_, ".producer.lock", O_PATH | O_NOFOLLOW | O_CLOEXEC);
      if (pin < 0 && errno == ENOENT) {
        lease_ = openat(directory_,
                        ".producer.lock",
                        O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC,
                        0600);
      } else if (pin >= 0) {
        if (!PrivateRegular(pin)) {
          close(pin);
          throw std::runtime_error("Observation producer lease object is unsafe.");
        }
        lease_ = open(("/proc/self/fd/" + std::to_string(pin)).c_str(),
                      O_WRONLY | O_NONBLOCK | O_CLOEXEC);
        struct stat original {
        }, opened{};
        const bool same = lease_ >= 0 && fstat(pin, &original) == 0 &&
                          fstat(lease_, &opened) == 0 && original.st_dev == opened.st_dev &&
                          original.st_ino == opened.st_ino;
        close(pin);
        if (!same) throw std::runtime_error("Observation producer lease changed unexpectedly.");
      }
      if (lease_ < 0 || !PrivateRegular(lease_) || flock(lease_, LOCK_EX | LOCK_NB))
        throw std::runtime_error("Observation directory producer lease is unavailable or unsafe.");
      CheckTarget();
    } catch (...) {
      Close();
      throw;
    }
  }
  ~SnapshotWriter() {
    Close();
  }
  void Publish(const std::string& json) {
    if (json.size() > 65536) throw std::runtime_error("Snapshot exceeds 64 KiB.");
    CheckTarget();
    const int fd = openat(
        directory_, temporary_.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (fd < 0) throw std::runtime_error("Cannot create private snapshot temporary file.");
    bool okay = true;
    size_t offset = 0;
    while (offset < json.size()) {
      ssize_t n = write(fd, json.data() + offset, json.size() - offset);
      if (n < 0 && errno == EINTR) continue;
      if (n <= 0) {
        okay = false;
        break;
      }
      offset += n;
    }
    if (close(fd)) okay = false;
    if (okay) {
      try {
        CheckTarget();
      } catch (...) {
        okay = false;
      }
    }
    if (!okay || renameat(directory_, temporary_.c_str(), directory_, "snapshot.json")) {
      unlinkat(directory_, temporary_.c_str(), 0);
      throw std::runtime_error("Atomic snapshot publication failed.");
    }
  }

 private:
  void CheckTarget() {
    struct stat s {};
    if (fstatat(directory_, "snapshot.json", &s, AT_SYMLINK_NOFOLLOW) < 0) {
      if (errno == ENOENT) return;
      throw std::runtime_error("Cannot inspect snapshot target.");
    }
    if (!S_ISREG(s.st_mode) || s.st_uid != geteuid() || (s.st_mode & 077) || s.st_nlink != 1)
      throw std::runtime_error("Existing snapshot target is unsafe.");
  }
  void Close() {
    if (lease_ >= 0) close(lease_);
    if (directory_ >= 0) close(directory_);
    lease_ = directory_ = -1;
  }
  int directory_ = -1, lease_ = -1;
  std::string temporary_;
};
}  // namespace

struct RuntimeObservation::Impl {
  std::string instance = ReadIdentity("/proc/sys/kernel/random/uuid");
  std::string boot = ReadIdentity("/proc/sys/kernel/random/boot_id");
  std::string time_namespace = TimeNamespace();
  std::unique_ptr<SnapshotWriter> writer;
  Value config = Object(), feedback = Object();
  std::set<std::string> loggers;
  std::deque<Value> logs, exits;
  uint64_t sequence = 0, log_sequence = 0, dropped = 0, truncated = 0, omitted_exits = 0;
  std::string state = "prepared", error, sensor, topic, expected_node, expected_gid;
  bool callback_failed = false;
  struct Ros {
    rclcpp::Context::SharedPtr context;
    rclcpp::Node::SharedPtr node;
    std::unique_ptr<rclcpp::executors::SingleThreadedExecutor> executor;
    rclcpp::Subscription<rcl_interfaces::msg::Log>::SharedPtr logs;
    rclcpp::Subscription<sensor_msgs::msg::JointState>::SharedPtr feedback;
    ~Ros() {
      try {
        if (executor && node) executor->remove_node(node);
        if (context && context->is_valid()) context->shutdown("Passive observation cleanup");
      } catch (...) {
      }
    }
  };
  std::unique_ptr<Ros> ros;

  void FeedbackState(const std::string& next) {
    Set(feedback, "state", Text(next));
    Set(feedback, "sample", Null());
  }
  void Disable() noexcept {
    try {
      state = "disabled";
      error = "Passive ROS observation failed; runtime process monitoring continues.";
      expected_gid.clear();
      FeedbackState(sensor.empty() ? "not_configured" : "unavailable");
      ros.reset();
    } catch (...) {
    }
  }
  void Graph() {
    if (sensor.empty()) return;
    auto publishers = ros->node->get_publishers_info_by_topic(topic);
    std::string gid;
    std::string next = "missing_publisher";
    if (publishers.size() > 1)
      next = "ambiguous";
    else if (publishers.size() == 1) {
      const auto& p = publishers.front();
      if (p.node_name() != expected_node || p.node_namespace() != "/" ||
          p.topic_type() != "sensor_msgs/msg/JointState")
        next = "wrong_publisher";
      else {
        gid = Gid(p.endpoint_gid().data(), p.endpoint_gid().size());
        next = "unavailable";
      }
    }
    if (gid != expected_gid) {
      expected_gid = gid;
      FeedbackState(next);
    } else if (gid.empty())
      FeedbackState(next);
  }
  void Feedback(const sensor_msgs::msg::JointState& message, const rclcpp::MessageInfo& info) {
    // Re-query before accepting a sample; a graph change invalidates all cached
    // data, including a queued sample from an endpoint that disappeared.
    Graph();
    const auto& rmw = info.get_rmw_message_info();
    const std::string gid = Gid(rmw.publisher_gid.data, RMW_GID_STORAGE_SIZE);
    if (expected_gid.empty() || gid != expected_gid) {
      FeedbackState("invalid");
      return;
    }
    const auto n = message.name.size();
    if (n == 0 || n > 64 || message.position.size() != n ||
        (!message.velocity.empty() && message.velocity.size() != n) ||
        (!message.effort.empty() && message.effort.size() != n) ||
        message.header.frame_id.size() > 256 || message.header.stamp.nanosec >= 1000000000) {
      FeedbackState("invalid");
      return;
    }
    std::set<std::string> names;
    size_t selected = n;
    for (size_t i = 0; i < n; ++i) {
      if (message.name[i].empty() || message.name[i].size() > 256 ||
          !names.insert(message.name[i]).second) {
        FeedbackState("invalid");
        return;
      }
      if (message.name[i] == sensor) selected = i;
    }
    for (const auto* values : {&message.position, &message.velocity, &message.effort})
      for (double value : *values)
        if (!std::isfinite(value) || std::abs(value) > 1e9) {
          FeedbackState("invalid");
          return;
        }
    if (selected == n) {
      FeedbackState("invalid");
      return;
    }
    Value sample = Object();
    Set(sample, "position", Number(message.position[selected]));
    Set(sample, "units", Text("radians"));
    Set(sample, "reference", Text("device_zero"));
    Set(sample, "source_stamp_sec", Number(message.header.stamp.sec));
    Set(sample, "source_stamp_nanosec", Number(message.header.stamp.nanosec));
    Set(sample, "frame_id", Text(Bounded(message.header.frame_id, 256)));
    Set(sample, "receipt_boottime_ns", Text(std::to_string(Now())));
    Set(sample, "publisher_gid", Text(gid));
    Set(sample, "acquisition_freshness", Text("unknown"));
    Set(feedback, "sample", sample);
    Set(feedback, "state", Text("available"));
  }
  void Log(const rcl_interfaces::msg::Log& message) {
    if (message.name.size() > 256 || !loggers.count(message.name)) return;
    if (message.stamp.nanosec >= 1000000000 ||
        (message.level != 10 && message.level != 20 && message.level != 30 && message.level != 40 &&
         message.level != 50)) {
      ++dropped;
      return;
    }
    Value v = Object();
    Set(v, "sequence", Number(++log_sequence));
    Set(v, "level", Number(message.level));
    Set(v, "name", Text(message.name));
    bool message_altered = false, file_altered = false, function_altered = false;
    Set(v, "message", Text(Bounded(message.msg, 512, &message_altered)));
    Set(v, "file", Text(Bounded(message.file, 128, &file_altered)));
    Set(v, "function", Text(Bounded(message.function, 128, &function_altered)));
    Set(v, "line", Number(message.line));
    Set(v, "stamp_sec", Number(message.stamp.sec));
    Set(v, "stamp_nanosec", Number(message.stamp.nanosec));
    Set(v, "receipt_boottime_ns", Text(std::to_string(Now())));
    Set(v, "runtime_association", Text("runtime_association_unverified"));
    if (message_altered || file_altered || function_altered) ++truncated;
    if (logs.size() == 32) {
      logs.pop_front();
      ++dropped;
    }
    logs.push_back(std::move(v));
  }
  void Start() {
    ros = std::make_unique<Ros>();
    ros->context = std::make_shared<rclcpp::Context>();
    // Context::init does not register the global rclcpp signal handlers.
    rclcpp::InitOptions init_options;
    init_options.auto_initialize_logging(false);
    init_options.shutdown_on_signal = false;
    ros->context->init(0, nullptr, init_options);
    rclcpp::NodeOptions options;
    options.context(ros->context)
        .use_global_arguments(false)
        .enable_rosout(false)
        .start_parameter_services(false)
        .start_parameter_event_publisher(false);
    ros->node = std::make_shared<rclcpp::Node>(
        "joshua_passive_observer_" + std::to_string(getpid()), options);
    rclcpp::ExecutorOptions executor_options;
    executor_options.context = ros->context;
    ros->executor = std::make_unique<rclcpp::executors::SingleThreadedExecutor>(executor_options);
    ros->executor->add_node(ros->node);
    ros->logs = ros->node->create_subscription<rcl_interfaces::msg::Log>(
        "/rosout",
        rclcpp::QoS(rclcpp::KeepLast(32)).best_effort().durability_volatile(),
        [this](rcl_interfaces::msg::Log::ConstSharedPtr message) {
          try {
            Log(*message);
          } catch (...) {
            callback_failed = true;
          }
        });
    if (!sensor.empty())
      ros->feedback = ros->node->create_subscription<sensor_msgs::msg::JointState>(
          topic,
          rclcpp::QoS(rclcpp::KeepLast(1)).best_effort().durability_volatile(),
          [this](sensor_msgs::msg::JointState::ConstSharedPtr message,
                 const rclcpp::MessageInfo& info) {
            try {
              Feedback(*message, info);
            } catch (...) {
              callback_failed = true;
            }
          });
    state = "observing";
  }
  void Publish(const std::vector<ProcessFact>& processes,
               size_t expected,
               const std::string& phase) {
    Value s = Object();
    Set(s, "schema_version", Number(1));
    Set(s, "instance_id", Text(instance));
    Set(s, "sequence", Number(++sequence));
    Set(s, "boot_id", Text(boot));
    Set(s, "time_namespace", Text(time_namespace));
    Set(s, "clock_kind", Text("CLOCK_BOOTTIME"));
    Set(s, "captured_boottime_ns", Text(std::to_string(Now())));
    Set(s, "build_version", Text(JOSHUA_VERSION));
    Set(s, "config", config);
    Set(s, "phase", Text(phase));
    Set(s, "expected_process_count", Number(expected));
    Value ps = Array();
    size_t count = 0;
    for (const auto& p : processes) {
      if (count++ >= 32) break;
      *ps.mutable_list_value()->add_values() = Process(p);
    }
    Set(s, "processes", ps);
    Set(s, "omitted_processes", Number(processes.size() - ps.list_value().values_size()));
    Value es = Array();
    for (const auto& e : exits) *es.mutable_list_value()->add_values() = e;
    Set(s, "exits", es);
    Set(s, "omitted_exits", Number(omitted_exits));
    Value observation = Object();
    Set(observation, "state", Text(state));
    Set(observation, "error", Text(error));
    Set(s, "observation", observation);
    Set(s, "feedback", feedback);
    std::string json;
    // JSON escaping can expand bounded strings. Evict the oldest logs until
    // the actual encoding fits, preserving current process/feedback facts.
    while (true) {
      Value ls = Object(), entries = Array();
      for (const auto& e : logs) *entries.mutable_list_value()->add_values() = e;
      Set(ls, "entries", entries);
      Set(ls, "dropped", Number(dropped));
      Set(ls, "truncated", Number(truncated));
      Set(ls, "latest_sequence", Number(log_sequence));
      Set(s, "logs", ls);
      json.clear();
      auto result = google::protobuf::util::MessageToJsonString(s, &json);
      if (!result.ok()) throw std::runtime_error("Cannot serialize observation.");
      if (json.size() + 1 <= 65536) break;
      if (logs.empty()) throw std::runtime_error("Snapshot facts exceed 64 KiB.");
      logs.pop_front();
      ++dropped;
    }
    writer->Publish(json + "\n");
  }
};

RuntimeObservation::RuntimeObservation(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
RuntimeObservation::~RuntimeObservation() = default;
absl::StatusOr<std::unique_ptr<RuntimeObservation>> RuntimeObservation::Prepare(
    const config::Config& cfg,
    const std::string& source,
    const std::string& directory,
    const std::string& sensor) {
  try {
    if (directory.empty() || source.size() > 4096 || sensor.size() > 128 ||
        cfg.general().name().size() > 256)
      return absl::InvalidArgumentError(
          "Require a private observation directory and bounded sensor/source names.");
    if (cfg.general().operation_mode() == config::General::MODE_SIMULATION)
      return absl::InvalidArgumentError(
          "Passive observation for simulation (including mirror mode) is unsupported.");
    auto impl = std::make_unique<Impl>();
    Set(impl->config, "source", Text(source));
    Set(impl->config, "name", Text(Bounded(cfg.general().name(), 256)));
    Set(impl->config, "id", Text(std::to_string(cfg.general().id())));
    Set(impl->config,
        "provenance",
        Text("Launcher-loaded config; children independently reload source path. No content hash "
             "or child-byte equivalence."));
    impl->sensor = sensor;
    Value selected_source = Object();
    Set(selected_source, "topic", Text(""));
    Set(selected_source, "node_name", Text(""));
    Set(selected_source, "node_namespace", Text("/"));
    Set(selected_source, "board_name", Text(""));
    Set(selected_source, "channel", Number(0));
    int matches = 0;
    std::vector<const ros2::node::Node*> nodes;
    for (const auto& a : cfg.robot().actions().single_actions()) nodes.push_back(&a.node());
    for (const auto& s : cfg.robot().perceptions().single_perceptions()) {
      nodes.push_back(&s.node());
      if (sensor.empty() || s.sensor_name() != sensor) continue;
      ++matches;
      if (s.sensor_type() != robot::perception::POSITION || !s.has_sts3215_encoder_config() ||
          s.node().node_type() != ros2::node::POSITION_PUBLISHER)
        return absl::InvalidArgumentError(
            "Selected sensor must be a POSITION encoder on POSITION_PUBLISHER.");
      int eligible = 0;
      for (const auto& p : s.node().publishers())
        if (p.ros2_data_type() == ros2::data_type::JOINT_STATE) {
          ++eligible;
          impl->topic = p.topic();
        }
      if (eligible != 1 || impl->topic.empty() || impl->topic.size() > 256)
        return absl::InvalidArgumentError(
            "Selected sensor must have exactly one bounded JOINT_STATE publisher topic.");
      if (impl->topic.front() != '/') impl->topic = "/" + impl->topic;
      bool at_start = true;
      for (size_t i = 1; i < impl->topic.size(); ++i) {
        const unsigned char c = impl->topic[i];
        if (c == '/') {
          if (at_start) return absl::InvalidArgumentError("Feedback topic has an empty token.");
          at_start = true;
          continue;
        }
        if (!(std::isalnum(c) || c == '_') || c >= 127 || (at_start && std::isdigit(c)))
          return absl::InvalidArgumentError("Feedback topic is not a valid absolute ROS topic.");
        at_start = false;
      }
      if (at_start || s.sts3215_encoder_config().board_name().size() > 256)
        return absl::InvalidArgumentError("Feedback topic or board identity is invalid.");
      impl->expected_node = NodeName(s.node());
      Set(selected_source, "topic", Text(impl->topic));
      Set(selected_source, "node_name", Text(impl->expected_node));
      Set(selected_source, "board_name", Text(s.sts3215_encoder_config().board_name()));
      Set(selected_source, "channel", Number(s.sts3215_encoder_config().channel()));
    }
    for (const auto& m : cfg.ai().models().single_models()) nodes.push_back(&m.node());
    for (const auto& d : cfg.ai().data_stores().single_data_stores()) nodes.push_back(&d.node());
    for (const auto& t : cfg.robot().trajectories().single_trajectories())
      nodes.push_back(&t.node());
    if (!sensor.empty() && matches != 1)
      return absl::InvalidArgumentError("Selected sensor identity is absent or duplicated.");
    size_t publishers = 0;
    for (const auto* n : nodes) {
      const auto name = NodeName(*n);
      if (name.size() > 128)
        return absl::InvalidArgumentError("Configured node name exceeds observation bound.");
      impl->loggers.insert(name);
      for (const auto& p : n->publishers()) {
        const auto topic =
            p.topic().empty() || p.topic().front() == '/' ? p.topic() : "/" + p.topic();
        if (!sensor.empty() && topic == impl->topic) ++publishers;
      }
    }
    if (!sensor.empty() && publishers != 1)
      return absl::InvalidArgumentError(
          "Selected feedback topic has multiple configured publishers.");
    Set(impl->feedback, "sensor_id", Text(sensor));
    Set(impl->feedback, "source", selected_source);
    impl->FeedbackState(sensor.empty() ? "not_configured" : "unavailable");
    Value limits = Array();
    for (const auto* text :
         {"ROS metadata associates a publisher; it does not authenticate hardware.",
          "Position is radians relative to device zero. It may be a leader sensor, not a "
          "controlled actuator measurement.",
          "Source stamp is preserved publication metadata; acquisition freshness and task "
          "association are unknown."})
      *limits.mutable_list_value()->add_values() = Text(text);
    Set(impl->feedback, "limitations", limits);
    impl->writer = std::make_unique<SnapshotWriter>(directory, impl->instance);
    return std::unique_ptr<RuntimeObservation>(new RuntimeObservation(std::move(impl)));
  } catch (const std::exception& e) {
    return absl::InvalidArgumentError(e.what());
  } catch (...) {
    return absl::InternalError("Observation preflight failed.");
  }
}
void RuntimeObservation::Start() noexcept {
  try {
    impl_->Start();
  } catch (...) {
    impl_->Disable();
  }
}
void RuntimeObservation::RecordExit(const ProcessFact& p, int status) noexcept {
  try {
    auto v = Process(p);
    Set(v, "exit_code", WIFEXITED(status) ? Number(WEXITSTATUS(status)) : Null());
    Set(v, "signal", WIFSIGNALED(status) ? Number(WTERMSIG(status)) : Null());
    if (impl_->exits.size() == 16) {
      impl_->exits.pop_front();
      ++impl_->omitted_exits;
    }
    impl_->exits.push_back(std::move(v));
  } catch (...) {
  }
}
void RuntimeObservation::Tick(const std::vector<ProcessFact>& processes,
                              size_t expected,
                              const std::string& phase) noexcept {
  try {
    if (impl_->ros) {
      impl_->Graph();
      impl_->ros->executor->spin_some(std::chrono::milliseconds(5));
      if (impl_->callback_failed) throw std::runtime_error("Observation callback failed");
      impl_->Graph();
    }
  } catch (...) {
    impl_->Disable();
  }
  try {
    impl_->Publish(processes, expected, phase);
  } catch (...) { /* Last atomic snapshot ages out; child monitoring continues. */
  }
}
}  // namespace node_generator
