#include "tools/mcp/image_observer_backend.h"
// Passive ROS image reader. No hardware factories, capture handles or launcher ownership.
#include <arpa/inet.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cstdlib>
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
#include "tools/mcp/ros_source_binding.h"

namespace joshua::mcp {
namespace {
using google::protobuf::Value;
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
  if (it == f.end()) throw std::runtime_error("Missing image metadata field");
  return it->second;
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
// Atomic replacement can unlink an inode already pinned by O_PATH. Its
// descriptor remains valid; multiple hard links are still forbidden.
using Binding = joshua::mcp::RosSourceBinding;
Binding Resolve(int root, const std::string& path, const std::string& camera, int domain) {
  auto result = joshua::mcp::ResolveImageSource(root, path, camera, domain);
  if (!result.ok()) throw std::runtime_error(std::string(result.status().message()));
  return *result;
}
class Observer {
 public:
  Observer(const Binding& b, const JpegEncoder& encode)
      : binding(b), encoder(encode), nonce(Identity("/proc/sys/kernel/random/uuid")) {
    clock = "CLOCK_BOOTTIME:" + Identity("/proc/sys/kernel/random/boot_id") + ":" + TimeNamespace();
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
    Subscribe();
  }
  void Subscribe() {
    subscription.reset();
    subscription = node->create_subscription<sensor_msgs::msg::Image>(
        binding.topic,
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
    if (next != gid) refresh_subscription = true;
    if (next != gid || next.empty())
      Clear("Expected sole camera publisher is missing, wrong, ambiguous or replaced");
    gid = next;
  }
  void Spin() {
    Graph();
    if (refresh_subscription) {
      // Humble can retain a matched reader without delivering replacement-writer
      // frames. Refresh it outside callbacks, preserving the endpoint/GID gates.
      Subscribe();
      refresh_subscription = false;
    }
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
    (*v.mutable_struct_value()->mutable_fields())["runtime_instance"].set_null_value(
        google::protobuf::NULL_VALUE);
    Set(v, "receipt_boottime_ns", std::to_string(receipt));
    Set(v, "clock_id", clock);
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
  bool refresh_subscription = false;
  std::string clock, nonce, gid, diagnostic = "No associated image received";
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
    if (argc != 7 || std::string(argv[1]) != "--root-fd" || std::string(argv[3]) != "--config" ||
        std::string(argv[5]) != "--camera-id")
      return 2;
    int directory = std::stoi(argv[2]);
    const std::string path = argv[4], camera = argv[6];
    struct stat d {};
    if (fstat(directory, &d) || !S_ISDIR(d.st_mode)) return 2;
    const std::string domain_text =
        std::getenv("ROS_DOMAIN_ID") ? std::getenv("ROS_DOMAIN_ID") : "0";
    if (domain_text.empty() || domain_text.size() > 3 ||
        domain_text.find_first_not_of("0123456789") != std::string::npos)
      return 2;
    const int domain = std::stoi(domain_text);
    if (domain > 232) return 2;
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
        auto binding = Resolve(directory, path, camera, domain);
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
        // Include encode/discovery in the candidate mapping race check, and re-check endpoint
        // identity.
        auto current = Resolve(directory, path, camera, domain);
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

}  // namespace joshua::mcp
