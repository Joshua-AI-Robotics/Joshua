#include "ros2/image_observer_backend.h"
// Passive ROS image reader. No hardware factories, capture handles or launcher ownership.
#include <arpa/inet.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <fstream>
#include <iostream>
#include <memory>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <optional>
#include <regex>
#include <stdexcept>
#include <string>
#include <vector>

#include "google/protobuf/struct.pb.h"
#include "google/protobuf/util/json_util.h"
#include "rclcpp/rclcpp.hpp"
#include "sensor_msgs/msg/image.hpp"

namespace ros2_utils {
namespace {
using google::protobuf::Value;
using Fields = google::protobuf::Map<std::string, Value>;
Value Object() {
  Value v;
  v.mutable_struct_value();
  return v;
}
void Set(Value& v, const std::string& k, const std::string& s) {
  (*v.mutable_struct_value()->mutable_fields())[k].set_string_value(s);
}
void Set(Value& v, const std::string& k, const char* s) {
  Set(v, k, std::string(s));
}
void Set(Value& v, const std::string& k, double n) {
  (*v.mutable_struct_value()->mutable_fields())[k].set_number_value(n);
}
void Flag(Value& v, const std::string& k, bool b) {
  (*v.mutable_struct_value()->mutable_fields())[k].set_bool_value(b);
}
uint64_t Now() {
  timespec t{};
  if (clock_gettime(CLOCK_BOOTTIME, &t)) throw std::runtime_error("CLOCK_BOOTTIME unavailable");
  return uint64_t(t.tv_sec) * 1000000000 + t.tv_nsec;
}
std::string Identity(const char* path) {
  std::ifstream f(path);
  std::string s;
  std::getline(f, s);
  if (!f || s.empty() || s.size() > 128)
    throw std::runtime_error("Linux clock identity unavailable");
  return s;
}
std::string TimeNamespace() {
  char b[128];
  auto n = readlink("/proc/self/ns/time", b, sizeof(b));
  if (n <= 0 || static_cast<size_t>(n) >= sizeof(b))
    throw std::runtime_error("Time namespace unavailable");
  return {b, static_cast<size_t>(n)};
}
std::string Json(const Value& v) {
  std::string s;
  if (!google::protobuf::util::MessageToJsonString(v, &s).ok())
    throw std::runtime_error("JSON encode failed");
  return s;
}
const Value& Field(const Value& v, const std::string& k) {
  const auto& f = v.struct_value().fields();
  auto it = f.find(k);
  if (it == f.end()) throw std::runtime_error("Missing manifest field");
  return it->second;
}
std::string Text(const Value& v, const std::string& k, size_t limit = 256) {
  const auto& f = Field(v, k);
  if (f.kind_case() != Value::kStringValue || f.string_value().size() > limit ||
      f.string_value().find('\0') != std::string::npos)
    throw std::runtime_error("Invalid manifest text");
  return f.string_value();
}
int Integer(const Value& v, const std::string& k, int maximum) {
  const auto& f = Field(v, k);
  if (f.kind_case() != Value::kNumberValue || f.number_value() < 0 || f.number_value() > maximum ||
      f.number_value() != static_cast<int>(f.number_value()))
    throw std::runtime_error("Invalid manifest number");
  return f.number_value();
}
std::string Gid(const uint8_t* bytes, size_t size) {
  const char* hex = "0123456789abcdef";
  std::string s;
  for (size_t i = 0; i < size; ++i) {
    s += hex[bytes[i] >> 4];
    s += hex[bytes[i] & 15];
  }
  return s;
}
bool Private(const struct stat& s) {
  return S_ISREG(s.st_mode) && s.st_uid == geteuid() && !(s.st_mode & 077) && s.st_nlink == 1;
}
struct Binding {
  std::string instance, camera, topic, node, ns, boot, time_ns;
  int domain;
  bool operator==(const Binding& b) const {
    return instance == b.instance && camera == b.camera && topic == b.topic && node == b.node &&
           ns == b.ns && domain == b.domain && boot == b.boot && time_ns == b.time_ns;
  }
};
Binding Manifest(int directory,
                 const std::string& camera,
                 const std::string& boot,
                 const std::string& time_ns) {
  int pin = openat(directory, "camera-source.json", O_PATH | O_NOFOLLOW | O_CLOEXEC);
  if (pin < 0) throw std::runtime_error("Camera manifest absent");
  struct stat original {
  }, content{};
  if (fstat(pin, &original) || !Private(original) || original.st_size <= 0 ||
      original.st_size > 16384) {
    close(pin);
    throw std::runtime_error("Unsafe camera manifest");
  }
  int fd =
      open(("/proc/self/fd/" + std::to_string(pin)).c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
  bool same = fd >= 0 && fstat(fd, &content) == 0 && Private(content) &&
              original.st_dev == content.st_dev && original.st_ino == content.st_ino;
  close(pin);
  if (!same) {
    if (fd >= 0) close(fd);
    throw std::runtime_error("Camera manifest changed while opening");
  }
  std::array<char, 16385> buffer{};
  size_t used = 0;
  while (used < buffer.size()) {
    auto n = read(fd, buffer.data() + used, buffer.size() - used);
    if (n < 0 && errno == EINTR) continue;
    if (n < 0) {
      close(fd);
      throw std::runtime_error("Manifest read failed");
    }
    if (!n) break;
    used += n;
  }
  close(fd);
  if (used > 16384) throw std::runtime_error("Oversized camera manifest");
  Value v;
  if (!google::protobuf::util::JsonStringToMessage(std::string(buffer.data(), used), &v).ok() ||
      v.kind_case() != Value::kStructValue || v.struct_value().fields_size() != 14)
    throw std::runtime_error("Invalid camera manifest schema");
  if (Integer(v, "schema_version", 1) != 1 || Text(v, "camera_id") != camera ||
      Text(v, "ros_type") != "sensor_msgs/msg/Image" || Text(v, "clock_kind") != "CLOCK_BOOTTIME" ||
      Text(v, "boot_id") != boot || Text(v, "time_namespace") != time_ns ||
      Text(v, "phase") != "monitoring" ||
      Field(v, "process_present").kind_case() != Value::kBoolValue ||
      !Field(v, "process_present").bool_value())
    throw std::runtime_error("Camera binding unavailable or clock incomparable");
  auto stamp = Text(v, "captured_boottime_ns", 20);
  if (!std::regex_match(stamp, std::regex("[0-9]{1,20}")))
    throw std::runtime_error("Invalid manifest time");
  uint64_t captured = std::stoull(stamp), now = Now();
  if (captured > now || now - captured > 3000000000ULL)
    throw std::runtime_error("Camera manifest stale or in the future");
  Binding b{Text(v, "instance_id", 128),
            camera,
            Text(v, "topic"),
            Text(v, "node_name", 128),
            Text(v, "node_namespace"),
            boot,
            time_ns,
            Integer(v, "ros_domain", 232)};
  if (b.instance.empty() || !std::regex_match(b.node, std::regex("[A-Za-z_][A-Za-z_0-9]*")) ||
      b.ns != "/" || !std::regex_match(b.topic, std::regex("(/[A-Za-z_][A-Za-z_0-9]*)+")))
    throw std::runtime_error("Invalid ROS camera binding");
  return b;
}
class Observer {
 public:
  Observer(const Binding& b, const JpegEncoder& encode)
      : binding(b), encoder(encode), nonce(Identity("/proc/sys/kernel/random/uuid")) {
    context = std::make_shared<rclcpp::Context>();
    rclcpp::InitOptions init;
    init.auto_initialize_logging(false);
    init.shutdown_on_signal = false;
    init.set_domain_id(b.domain);
    context->init(0, nullptr, init);
    rclcpp::NodeOptions options;
    options.context(context)
        .use_global_arguments(false)
        .enable_rosout(false)
        .start_parameter_services(false)
        .start_parameter_event_publisher(false);
    node = std::make_shared<rclcpp::Node>("joshua_image_observer_" + std::to_string(getpid()),
                                          options);
    rclcpp::ExecutorOptions eo;
    eo.context = context;
    executor = std::make_unique<rclcpp::executors::SingleThreadedExecutor>(eo);
    executor->add_node(node);
    subscription = node->create_subscription<sensor_msgs::msg::Image>(
        b.topic,
        rclcpp::QoS(1).best_effort().durability_volatile(),
        [this](sensor_msgs::msg::Image::ConstSharedPtr m, const rclcpp::MessageInfo& info) {
          Receive(*m, info);
        });
  }
  ~Observer() {
    if (context) context->shutdown("image reader closed");
  }
  void Clear(const std::string& why) {
    sample.reset();
    jpeg.clear();
    diagnostic = why;
  }
  void Graph() {
    auto endpoints = node->get_publishers_info_by_topic(binding.topic);
    std::string next;
    if (endpoints.size() == 1) {
      const auto& e = endpoints.front();
      if (e.node_name() == binding.node && e.node_namespace() == binding.ns &&
          e.topic_type() == "sensor_msgs/msg/Image")
        next = Gid(e.endpoint_gid().data(), e.endpoint_gid().size());
    }
    if (next != gid || next.empty())
      Clear("Expected sole camera publisher is missing, wrong, ambiguous or replaced");
    gid = next;
  }
  void Spin() {
    Graph();
    executor->spin_some(std::chrono::milliseconds(5));
    Graph();
  }
  Value Reply(uint64_t query, const std::string& camera) {
    Graph();
    Value v = Object();
    Set(v, "query_id", double(query));
    Set(v, "camera_id", camera);
    Flag(v, "reset", false);
    Set(v, "diagnostic", diagnostic);
    Set(v, "reason", sample ? "" : "no_data");
    Set(v,
        "availability",
        sample ? (Now() - receipt > 3000000000ULL ? "stale" : "fresh") : "unavailable");
    if (!sample) return v;
    if (jpeg.empty()) {
      cv::Mat packed(sample->height,
                     sample->width,
                     CV_8UC3,
                     const_cast<uint8_t*>(sample->data.data()),
                     sample->step);
      cv::Mat bgr;
      if (sample->encoding == "rgb8")
        cv::cvtColor(packed, bgr, cv::COLOR_RGB2BGR);
      else
        bgr = packed;
      const double scale = std::min({1.0, 640.0 / sample->width, 480.0 / sample->height});
      width = std::max(1, int(sample->width * scale));
      height = std::max(1, int(sample->height * scale));
      cv::Mat output;
      if (scale < 1)
        cv::resize(bgr, output, cv::Size(width, height), 0, 0, cv::INTER_AREA);
      else
        output = bgr;
      if (!encoder(output, jpeg) || jpeg.empty() || jpeg.size() > 512 * 1024) {
        Clear("JPEG encoding failed or exceeded 512 KiB");
        throw std::runtime_error(diagnostic);
      }
    }
    Set(v, "runtime_instance", binding.instance);
    Set(v, "receipt_boottime_ns", std::to_string(receipt));
    Set(v, "clock_id", "CLOCK_BOOTTIME:" + binding.boot + ":" + binding.time_ns);
    Set(v, "source_stamp_sec", double(sample->header.stamp.sec));
    Set(v, "source_stamp_nanosec", double(sample->header.stamp.nanosec));
    Set(v, "sample_id", nonce + ":" + std::to_string(counter));
    Set(v, "width", double(width));
    Set(v, "height", double(height));
    Set(v, "source_width", double(sample->width));
    Set(v, "source_height", double(sample->height));
    Set(v, "source_encoding", sample->encoding);
    Set(v, "frame_id", sample->header.frame_id);
    Set(v, "publisher_gid", gid);
    Set(v, "topic", binding.topic);
    Set(v, "node_name", binding.node);
    Set(v, "node_namespace", binding.ns);
    Set(v, "ros_domain", double(binding.domain));
    return v;
  }
  Binding binding;
  std::vector<uint8_t> jpeg;

 private:
  void Receive(const sensor_msgs::msg::Image& m, const rclcpp::MessageInfo& info) noexcept {
    try {
      Graph();
      if (gid.empty() ||
          Gid(info.get_rmw_message_info().publisher_gid.data, RMW_GID_STORAGE_SIZE) != gid) {
        Clear("Message publisher GID differs from expected endpoint");
        return;
      }
      const uint64_t row = uint64_t(m.width) * 3, bytes = uint64_t(m.step) * m.height;
      if (!m.width || !m.height || m.width > 1920 || m.height > 1080 ||
          (m.encoding != "rgb8" && m.encoding != "bgr8") || m.step < row ||
          bytes > 8 * 1024 * 1024 || bytes != m.data.size() || m.is_bigendian > 1 ||
          m.header.stamp.nanosec >= 1000000000 || m.header.frame_id.size() > 256 ||
          m.header.frame_id.find('\0') != std::string::npos) {
        Clear("Malformed or oversized image layout");
        return;
      }
      // Copy only after validation; DDS allocations before the callback are outside this bound.
      sample = m;
      jpeg.clear();
      receipt = Now();
      ++counter;
      diagnostic = "";
    } catch (...) {
      Clear("Image callback failed");
    }
  }
  JpegEncoder encoder;
  std::string nonce, gid, diagnostic = "No associated image received";
  uint64_t receipt = 0, counter = 0;
  int width = 0, height = 0;
  std::optional<sensor_msgs::msg::Image> sample;
  rclcpp::Context::SharedPtr context;
  rclcpp::Node::SharedPtr node;
  std::unique_ptr<rclcpp::executors::SingleThreadedExecutor> executor;
  rclcpp::Subscription<sensor_msgs::msg::Image>::SharedPtr subscription;
};
void Write(const void* bytes, size_t size) {
  const auto* p = static_cast<const char*>(bytes);
  while (size) {
    auto n = write(STDOUT_FILENO, p, size);
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0) throw std::runtime_error("Image response write failed");
    size -= n;
    p += n;
  }
}
void Reply(Value v, const std::vector<uint8_t>& jpeg) {
  auto json = Json(v);
  if (json.size() > 16384 || jpeg.size() > 512 * 1024)
    throw std::runtime_error("Image response limit exceeded");
  uint32_t header[] = {htonl(json.size()), htonl(jpeg.size())};
  Write(header, sizeof(header));
  Write(json.data(), json.size());
  Write(jpeg.data(), jpeg.size());
}
Value Error(uint64_t query,
            const std::string& camera,
            const std::string& reason,
            const std::string& text,
            bool reset) {
  auto v = Object();
  Set(v, "query_id", double(query));
  Set(v, "camera_id", camera);
  Set(v, "availability", "unavailable");
  Set(v, "reason", reason);
  Set(v, "diagnostic", text.substr(0, 512));
  Flag(v, "reset", reset);
  return v;
}
}  // namespace
int RunImageObserver(int argc, char** argv, JpegEncoder encoder) {
  if (!encoder)
    encoder = [](const cv::Mat& image, std::vector<uint8_t>& bytes) {
      return cv::imencode(".jpg", image, bytes, {cv::IMWRITE_JPEG_QUALITY, 80});
    };
  try {
    if (argc != 5 || std::string(argv[1]) != "--directory-fd" ||
        std::string(argv[3]) != "--camera-id")
      return 2;
    int directory = std::stoi(argv[2]);
    std::string camera = argv[4];
    struct stat d {};
    if (fstat(directory, &d) || !S_ISDIR(d.st_mode) || d.st_uid != geteuid() || d.st_mode & 077 ||
        camera.rfind("sensor:", 0) != 0 || camera.size() > 512 || camera.size() <= 7 ||
        camera.find('\0') != std::string::npos)
      return 2;
    const auto boot = Identity("/proc/sys/kernel/random/boot_id"), time_ns = TimeNamespace();
    std::unique_ptr<Observer> observer;
    std::string input;
    for (;;) {
      if (observer) {
        try {
          observer->Spin();
        } catch (...) {
          observer.reset();
        }
      }
      pollfd fd{STDIN_FILENO, POLLIN, 0};
      if (poll(&fd, 1, 10) < 0) {
        if (errno == EINTR) continue;
        return 3;
      }
      if (fd.revents & POLLHUP) return 0;
      if (!(fd.revents & POLLIN)) continue;
      char ch;
      if (read(STDIN_FILENO, &ch, 1) != 1) return 0;
      if (ch != '\n') {
        if (ch < '0' || ch > '9' || input.size() >= 12) return 2;
        input += ch;
        continue;
      }
      if (input.empty()) return 2;
      uint64_t query = std::stoull(input);
      input.clear();
      try {
        auto binding = Manifest(directory, camera, boot, time_ns);
        if (observer && !(observer->binding == binding)) {
          observer.reset();
          Reply(Error(query,
                      camera,
                      "no_data",
                      "Camera binding changed; retry with a fresh reader context",
                      true),
                {});
          continue;
        }
        if (!observer) observer = std::make_unique<Observer>(binding, encoder);
        observer->Spin();
        auto response = observer->Reply(query, camera);
        // Include encode/discovery in the binding race check, and re-check endpoint identity.
        auto current = Manifest(directory, camera, boot, time_ns);
        if (!(current == binding)) {
          observer.reset();
          Reply(Error(query, camera, "no_data", "Camera binding changed during query", true), {});
          continue;
        }
        observer->Graph();
        // Rebuild after graph validation so a removed endpoint cannot retain old bytes.
        response = observer->Reply(query, camera);
        Reply(response,
              Field(response, "availability").string_value() == "unavailable"
                  ? std::vector<uint8_t>{}
                  : observer->jpeg);
      } catch (const std::exception& error) {
        observer.reset();
        Reply(Error(query, camera, "no_data", error.what(), true), {});
      }
    }
  } catch (...) {
    return 3;
  }
}

}  // namespace ros2_utils
