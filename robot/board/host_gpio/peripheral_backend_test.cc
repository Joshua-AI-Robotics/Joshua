#include "robot/board/host_gpio/peripheral_backend.h"

#include <map>

#include "gtest/gtest.h"
namespace robot::board {
namespace {
class System final : public PeripheralSystem {
 public:
  std::vector<std::string> chips{"/sys/pwmchip7"};
  std::map<std::string, std::string> values;
  bool exported = false;
  std::string fail;
  std::vector<std::string> writes;
  std::vector<std::string> PwmChips() override {
    return chips;
  }
  absl::StatusOr<std::string> Controller(const std::string&, bool) override {
    return "pwm@98000";
  }
  absl::StatusOr<std::string> Read(const std::string&) override {
    return "4\n";
  }
  bool Exists(const std::string&) override {
    return exported;
  }
  absl::Status Write(const std::string& path, const std::string& value) override {
    writes.push_back(path + "=" + value);
    if (!fail.empty() && path.find(fail) != std::string::npos)
      return absl::UnavailableError("injected write failure");
    if (path.find("/unexport") != std::string::npos)
      exported = false;
    else if (path.find("/export") != std::string::npos)
      exported = true;
    values[path] = value;
    return absl::OkStatus();
  }
};
PeripheralResource Config() {
  PeripheralResource r;
  r.config.set_name("fan");
  r.config.set_purpose("fan test");
  r.config.mutable_pwm()->set_frequency_hz(1000);
  r.config.mutable_pwm()->set_initial_duty_cycle(0.25);
  r.pins.push_back(*PinProfiles().front().FindPin(32).value());
  return r;
}
TEST(PwmBackendTest, InitializesDisabledAndReleasesEvenWithRetainedHandle) {
  auto io = std::make_shared<System>();
  auto backend = MakeLinuxPeripheralBackend(io);
  auto output = backend->Pwm(Config());
  ASSERT_TRUE(output.ok()) << output.status();
  EXPECT_EQ(io->values["/sys/pwmchip7/pwm0/enable"], "0");
  EXPECT_EQ(io->values["/sys/pwmchip7/pwm0/period"], "1000000");
  EXPECT_EQ(io->values["/sys/pwmchip7/pwm0/duty_cycle"], "250000");
  EXPECT_TRUE((*output)->Enable().ok());
  EXPECT_EQ(io->values["/sys/pwmchip7/pwm0/enable"], "1");
  EXPECT_FALSE((*output)->SetDutyCycle(1.1).ok());
  ASSERT_TRUE((*output)->Close().ok());
  EXPECT_FALSE(io->exported);
  EXPECT_FALSE((*output)->Enable().ok());
}
TEST(PwmBackendTest, DoesNotTakeExistingExportsAndRollsBackPartialConfiguration) {
  auto io = std::make_shared<System>();
  auto backend = MakeLinuxPeripheralBackend(io);
  io->exported = true;
  EXPECT_FALSE(backend->Pwm(Config()).ok());
  EXPECT_TRUE(io->writes.empty());
  io->exported = false;
  io->fail = "/period";
  EXPECT_FALSE(backend->Pwm(Config()).ok());
  EXPECT_FALSE(io->exported);
  io->fail.clear();
  io->chips.push_back("/sys/pwmchip8");
  const auto n = io->writes.size();
  EXPECT_FALSE(backend->Pwm(Config()).ok());
  EXPECT_EQ(io->writes.size(), n);
}
}  // namespace
}  // namespace robot::board
