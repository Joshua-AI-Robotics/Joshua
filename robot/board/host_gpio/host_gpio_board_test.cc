#include "robot/board/host_gpio/host_gpio_board.h"

#include <utility>
#include <vector>

#include "gtest/gtest.h"

namespace robot::board {
namespace {
struct State {
  std::vector<std::string> events;
  int fail_request = -1;
  bool fail_write = false;
  bool fail_platform = false;
  int requests = 0;
};
class Line final : public GpioLine {
 public:
  Line(std::shared_ptr<State> state, std::string name, bool value)
      : state_(std::move(state)), name_(std::move(name)), value_(value) {}
  ~Line() override {
    state_->events.push_back("release " + name_);
  }
  absl::StatusOr<bool> Read() override {
    return value_;
  }
  absl::Status Write(bool value) override {
    state_->events.push_back("write " + name_ + (value ? " 1" : " 0"));
    if (state_->fail_write) return absl::UnavailableError("disconnected");
    value_ = value;
    return absl::OkStatus();
  }

 private:
  std::shared_ptr<State> state_;
  std::string name_;
  bool value_;
};
class Backend final : public GpioBackend {
 public:
  std::shared_ptr<State> state = std::make_shared<State>();
  absl::Status CheckPlatform(const PinProfile&) override {
    if (state->fail_platform) return absl::FailedPreconditionError("wrong platform");
    return absl::OkStatus();
  }
  absl::StatusOr<std::unique_ptr<GpioLine>> Request(const PinResource& pin) override {
    if (state->requests++ == state->fail_request) return absl::AlreadyExistsError("busy");
    state->events.push_back("acquire " + pin.config.name());
    return std::unique_ptr<GpioLine>(
        new Line(state, pin.config.name(), pin.config.initial_value()));
  }
};
Board Config() {
  Board b;
  b.set_name("local");
  b.set_board_type(HOST_GPIO);
  auto* c = b.mutable_host_gpio_config();
  c->set_profile_id("raspberrypi/pi5");
  c->set_profile_version(1);
  for (auto [name, n] :
       std::vector<std::pair<std::string, int>>{{"led", 11}, {"relay", 13}, {"switch", 15}}) {
    auto* p = c->add_pins();
    p->set_name(name);
    p->set_purpose("test " + name);
    p->mutable_pin()->set_header_pin(n);
    p->set_direction(name == "switch" ? DIGITAL_INPUT : DIGITAL_OUTPUT);
    if (p->direction() == DIGITAL_OUTPUT) {
      p->set_initial_value(true);
      p->set_shutdown_value(false);
    }
  }
  return b;
}
TEST(HostGpioBoardTest, SharesHandlesAndInvalidatesOnTeardown) {
  auto backend = std::make_shared<Backend>();
  HostGpioBoard board(backend);
  ASSERT_TRUE(board.Init(Config()).ok());
  auto pin = board.OpenDigitalPin("led");
  ASSERT_TRUE(pin.ok());
  EXPECT_EQ((*board.OpenDigitalPin("led")).get(), pin->get());
  EXPECT_EQ((*pin)->resource().config.purpose(), "test led");
  ASSERT_TRUE((*pin)->Read().ok());
  EXPECT_TRUE(*(*pin)->Read());
  EXPECT_TRUE((*pin)->Write(false).ok());
  EXPECT_FALSE(*(*pin)->Read());
  auto input = board.OpenDigitalPin("switch");
  ASSERT_TRUE(input.ok());
  EXPECT_FALSE((*input)->Write(true).ok());
  EXPECT_FALSE(board.OpenDigitalPin("missing").ok());
  EXPECT_EQ(board.OpenChannel(0).status().code(), absl::StatusCode::kUnimplemented);
  ASSERT_TRUE(board.Teardown().ok());
  EXPECT_FALSE((*pin)->Read().ok());
  EXPECT_FALSE((*pin)->Write(true).ok());
  EXPECT_FALSE(board.Init(Config()).ok());
  EXPECT_TRUE(board.Teardown().ok());
  const auto& e = backend->state->events;
  EXPECT_EQ(std::vector<std::string>(e.end() - 5, e.end()),
            (std::vector<std::string>{
                "release switch", "write relay 0", "release relay", "write led 0", "release led"}));
}
TEST(HostGpioBoardTest, PartialAcquisitionUnwindsInReverseOrder) {
  auto backend = std::make_shared<Backend>();
  backend->state->fail_request = 2;
  HostGpioBoard board(backend);
  EXPECT_EQ(board.Init(Config()).code(), absl::StatusCode::kAlreadyExists);
  EXPECT_FALSE(board.OpenDigitalPin("led").ok());
  EXPECT_EQ(backend->state->events,
            (std::vector<std::string>{"acquire led",
                                      "acquire relay",
                                      "write relay 0",
                                      "release relay",
                                      "write led 0",
                                      "release led"}));
}
TEST(HostGpioBoardTest, InvalidConfigAndWrongPlatformNeverAcquire) {
  auto backend = std::make_shared<Backend>();
  HostGpioBoard board(backend);
  auto c = Config();
  c.mutable_host_gpio_config()->mutable_pins(2)->mutable_pin()->set_header_pin(1);
  EXPECT_FALSE(board.Init(c).ok());
  EXPECT_EQ(backend->state->requests, 0);
  backend->state->fail_platform = true;
  EXPECT_FALSE(board.Init(Config()).ok());
  EXPECT_EQ(backend->state->requests, 0);
}
TEST(HostGpioBoardTest, ShutdownAttemptsAllPinsAndPreservesFailures) {
  auto backend = std::make_shared<Backend>();
  HostGpioBoard board(backend);
  ASSERT_TRUE(board.Init(Config()).ok());
  backend->state->fail_write = true;
  EXPECT_FALSE(board.EmergencyStop().ok());
  EXPECT_FALSE(board.Teardown().ok());
  EXPECT_EQ(backend->state->events.back(), "release led");
  EXPECT_EQ(backend->state->events.size(), 8);
}
TEST(HostGpioBoardTest, IoFailureLatchesAndDestructorReleasesOtherPins) {
  auto backend = std::make_shared<Backend>();
  std::shared_ptr<DigitalPin> retained;
  {
    HostGpioBoard board(backend);
    ASSERT_TRUE(board.Init(Config()).ok());
    retained = *board.OpenDigitalPin("led");
    backend->state->fail_write = true;
    EXPECT_FALSE(retained->Write(true).ok());
    backend->state->fail_write = false;
    EXPECT_FALSE(retained->Write(true).ok());
  }
  EXPECT_FALSE(retained->Read().ok());
  EXPECT_EQ(backend->state->events.back(), "release relay");
}
}  // namespace
}  // namespace robot::board
