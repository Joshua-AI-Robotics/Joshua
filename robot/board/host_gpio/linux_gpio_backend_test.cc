#include "robot/board/host_gpio/linux_gpio_backend.h"

#include <linux/gpio.h>

#include <cerrno>
#include <cstdio>
#include <map>
#include <set>
#include <utility>

#include "gtest/gtest.h"

namespace robot::board {
namespace {
class System final : public GpioSystem {
 public:
  std::vector<std::string> paths = {"/dev/gpiochip7"};
  std::string compatible = "raspberrypi,5-model-b";
  std::string label = "pinctrl-rp1";
  std::string line_name = "GPIO17";
  uint32_t runtime_offset = 3;
  uint64_t line_flags = 0;
  int request_error = 0;
  int info_error = 0;
  bool fail_open = false;
  bool value = false;
  gpio_v2_line_request requested{};
  int requests = 0;
  int writes = 0;
  int next_fd = 10;
  std::set<int> open_fds;
  std::map<int, std::string> fd_paths;
  std::vector<std::string> Chips() override {
    return paths;
  }
  std::string Compatible() override {
    return compatible;
  }
  int Open(const std::string& path) override {
    if (fail_open) {
      errno = EACCES;
      return -1;
    }
    const int fd = next_fd++;
    open_fds.insert(fd);
    fd_paths[fd] = path;
    return fd;
  }
  void Close(int fd) override {
    EXPECT_EQ(open_fds.erase(fd), 1);
  }
  int Ioctl(int fd, unsigned long op, void* arg) override {
    EXPECT_TRUE(open_fds.count(fd));
    if (op == GPIO_GET_CHIPINFO_IOCTL) {
      auto* info = static_cast<gpiochip_info*>(arg);
      std::snprintf(info->label, sizeof(info->label), "%s", label.c_str());
      info->lines = 8;
    } else if (op == GPIO_V2_GET_LINEINFO_IOCTL) {
      if (info_error) {
        errno = info_error;
        return -1;
      }
      auto* line = static_cast<gpio_v2_line_info*>(arg);
      if (line->offset == runtime_offset) {
        std::snprintf(line->name, sizeof(line->name), "%s", line_name.c_str());
        line->flags = line_flags;
      }
    } else if (op == GPIO_V2_GET_LINE_IOCTL) {
      ++requests;
      if (request_error) {
        errno = request_error;
        return -1;
      }
      auto* request = static_cast<gpio_v2_line_request*>(arg);
      requested = *request;
      request->fd = next_fd++;
      open_fds.insert(request->fd);
      value = request->config.num_attrs && request->config.attrs[0].attr.values;
    } else if (op == GPIO_V2_LINE_GET_VALUES_IOCTL) {
      auto* values = static_cast<gpio_v2_line_values*>(arg);
      EXPECT_EQ(values->mask, 1);
      values->bits = value ? 1 : 0;
    } else if (op == GPIO_V2_LINE_SET_VALUES_IOCTL) {
      auto* values = static_cast<gpio_v2_line_values*>(arg);
      EXPECT_EQ(values->mask, 1);
      value = (values->bits & 1) != 0;
      ++writes;
    } else {
      ADD_FAILURE() << "Unexpected ioctl";
      errno = ENOTTY;
      return -1;
    }
    return 0;
  }
};
PinResource Resource(bool output = true) {
  DigitalPinConfig c;
  c.set_name("ready");
  c.set_purpose("status indicator");
  c.mutable_pin()->set_header_pin(11);
  c.set_direction(output ? DIGITAL_OUTPUT : DIGITAL_INPUT);
  c.set_active_low(true);
  c.set_bias(DIGITAL_BIAS_PULL_UP);
  if (output) {
    c.set_initial_value(true);
    c.set_shutdown_value(false);
  }
  return *ResolvePin(PinProfiles().front(), c);
}
TEST(LinuxGpioBackendTest, AcquiresByNameWithAtomicLogicalInitialValueAndReleasesDescriptors) {
  auto system = std::make_shared<System>();
  LinuxGpioBackend backend(system);
  ASSERT_TRUE(backend.CheckPlatform(PinProfiles().front()).ok());
  {
    auto line = backend.Request(Resource());
    ASSERT_TRUE(line.ok()) << line.status();
    EXPECT_EQ(system->requested.offsets[0], 3);  // Not catalog offset 17 or chip0.
    EXPECT_EQ(system->requested.num_lines, 1);
    EXPECT_EQ(
        system->requested.config.flags,
        GPIO_V2_LINE_FLAG_OUTPUT | GPIO_V2_LINE_FLAG_ACTIVE_LOW | GPIO_V2_LINE_FLAG_BIAS_PULL_UP);
    EXPECT_EQ(system->requested.config.attrs[0].attr.id, GPIO_V2_LINE_ATTR_ID_OUTPUT_VALUES);
    EXPECT_EQ(system->requested.config.attrs[0].mask, 1);
    EXPECT_TRUE(*(*line)->Read());
    EXPECT_TRUE((*line)->Write(false).ok());
    EXPECT_FALSE(*(*line)->Read());
    EXPECT_EQ(system->open_fds.size(), 1);  // Only exclusive line request remains.
  }
  EXPECT_TRUE(system->open_fds.empty());
}
TEST(LinuxGpioBackendTest, InputDoesNotApplyOutputValueOrAllowWrites) {
  auto system = std::make_shared<System>();
  LinuxGpioBackend backend(system);
  auto line = backend.Request(Resource(false));
  ASSERT_TRUE(line.ok());
  EXPECT_EQ(system->requested.config.num_attrs, 0);
  EXPECT_TRUE(system->requested.config.flags & GPIO_V2_LINE_FLAG_INPUT);
  EXPECT_FALSE((*line)->Write(true).ok());
  EXPECT_EQ(system->writes, 0);
}
TEST(LinuxGpioBackendTest, RejectsWrongPlatformIncludingSubstringAndThirdPartyCarrier) {
  auto system = std::make_shared<System>();
  LinuxGpioBackend backend(system);
  system->compatible = "raspberrypi,5-model-b-other";
  EXPECT_FALSE(backend.CheckPlatform(PinProfiles().front()).ok());
  system->compatible = "nvidia,p3509-0000+p3767-0005";
  EXPECT_FALSE(backend.CheckPlatform(PinProfiles()[1]).ok());
  system->compatible = std::string("other\0", 6) + "raspberrypi,5-model-b";
  EXPECT_TRUE(backend.CheckPlatform(PinProfiles().front()).ok());
  system->compatible = "nvidia,p3768-0000+p3767-0005-super";
  EXPECT_TRUE(backend.CheckPlatform(PinProfiles()[1]).ok());
}
TEST(LinuxGpioBackendTest, MissingOrAmbiguousControllerAndLineNeverAcquire) {
  auto system = std::make_shared<System>();
  LinuxGpioBackend backend(system);
  system->label = "other";
  EXPECT_EQ(backend.Request(Resource()).status().code(), absl::StatusCode::kNotFound);
  system->label = "pinctrl-rp1";
  system->line_name = "wrong";
  EXPECT_FALSE(backend.Request(Resource()).ok());
  system->line_name = "GPIO17";
  system->paths.push_back("/dev/gpiochip8");
  EXPECT_EQ(backend.Request(Resource()).status().code(), absl::StatusCode::kFailedPrecondition);
  EXPECT_EQ(system->requests, 0);
  EXPECT_TRUE(system->open_fds.empty());
}
TEST(LinuxGpioBackendTest, BusyPermissionAndUnsupportedAbiCloseEveryDescriptor) {
  auto system = std::make_shared<System>();
  LinuxGpioBackend backend(system);
  system->line_flags = GPIO_V2_LINE_FLAG_USED;
  EXPECT_EQ(backend.Request(Resource()).status().code(), absl::StatusCode::kAlreadyExists);
  system->line_flags = 0;
  system->request_error = EBUSY;
  EXPECT_EQ(backend.Request(Resource()).status().code(), absl::StatusCode::kAlreadyExists);
  system->request_error = 0;
  system->info_error = ENOTTY;
  EXPECT_EQ(backend.Request(Resource()).status().code(), absl::StatusCode::kUnimplemented);
  system->info_error = 0;
  system->fail_open = true;
  EXPECT_EQ(backend.Request(Resource()).status().code(), absl::StatusCode::kPermissionDenied);
  EXPECT_TRUE(system->open_fds.empty());
}
}  // namespace
}  // namespace robot::board
