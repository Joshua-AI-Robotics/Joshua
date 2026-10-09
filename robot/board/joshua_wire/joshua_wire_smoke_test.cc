// End-to-end CLI regression tests: launch the real smoke binary on an allocated
// Linux PTY backed by the actual AM243 software-only firmware handler. No real
// serial device, GPIO, ROS node or firmware flashing is involved. Temporary
// configs/output live in Bazel's test directory and are removed after each test.
#include <fcntl.h>
#include <poll.h>
#include <spawn.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#include <chrono>
#include <csignal>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "firmware/am243/joshua_dual_transport/src/joshua_commands.h"
#include "firmware/common/joshua_wire_endpoint.h"
#include "gtest/gtest.h"

extern char** environ;

namespace {
using Bytes = std::vector<uint8_t>;
enum class Fault { kNone, kLostEnableReply, kWrongMessageId, kLostEstopReply, kSignalAfterEnable };

class SmokeCliTest : public ::testing::Test {
 protected:
  void SetUp() override {
    const char* tmp = std::getenv("TEST_TMPDIR");
    const char* runfiles = std::getenv("TEST_SRCDIR");
    const char* workspace = std::getenv("TEST_WORKSPACE");
    ASSERT_NE(tmp, nullptr);
    ASSERT_NE(runfiles, nullptr);
    ASSERT_NE(workspace, nullptr);
    binary = std::string(runfiles) + "/" + workspace + "/robot/board/joshua_wire/joshua_wire_smoke";
    ASSERT_EQ(access(binary.c_str(), X_OK), 0);
    std::string pattern = std::string(tmp) + "/jw-cli-XXXXXX";
    ASSERT_NE(mkdtemp(pattern.data()), nullptr);
    directory = pattern;
    master = posix_openpt(O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
    ASSERT_GE(master, 0);
    ASSERT_EQ(grantpt(master), 0);
    ASSERT_EQ(unlockpt(master), 0);
    const char* port = ptsname(master);
    ASSERT_NE(port, nullptr);
    // Keep a raw slave open so the master does not see HUP before CLI startup.
    slave = open(port, O_RDWR | O_NOCTTY | O_CLOEXEC);
    ASSERT_GE(slave, 0);
    termios attributes{};
    ASSERT_EQ(tcgetattr(slave, &attributes), 0);
    cfmakeraw(&attributes);
    ASSERT_EQ(tcsetattr(slave, TCSANOW, &attributes), 0);
    std::ofstream config(directory + "/config.pbtxt");
    config << "robot { boards { name: \"pty\" board_type: AM243 protocol: JOSHUA_WIRE "
              "firmware { min_proto_version: 2 } "
              "comm { comm_type: SERIAL transport_type: MESSAGE serial_config { port: \""
           << port
           << "\" baudrate: 115200 } } channels { index: 0 drive: STEP_DIR step_dir { "
              "step_pin: 2 dir_pin: 3 enable_pin: 4 max_pulse_rate_hz: 1000 } } } }";
    config.close();
    ASSERT_TRUE(config.good());
    jw_endpoint_init(&endpoint);
  }

  void TearDown() override {
    if (child > 0) {
      kill(child, SIGKILL);
      waitpid(child, nullptr, 0);
    }
    if (slave >= 0) close(slave);
    if (master >= 0) close(master);
    if (!directory.empty()) {
      unlink((directory + "/config.pbtxt").c_str());
      unlink((directory + "/output.txt").c_str());
      rmdir(directory.c_str());
    }
  }

  void Respond(const Bytes& bytes) {
    jw_frame_t request{};
    ASSERT_EQ(jw_decode_frame(bytes.data(), bytes.size(), &request), JW_RESULT_OK);
    commands.push_back(request.cmd);
    sessions.push_back(request.session_id);
    uint8_t response[JW_MAX_FRAME_LEN];
    int size = jw_endpoint_process(&endpoint,
                                   bytes.data(),
                                   bytes.size(),
                                   response,
                                   sizeof(response),
                                   JoshuaCommand,
                                   JoshuaReset,
                                   &state);
    ASSERT_GT(size, 0);
    if (request.cmd == JW_CMD_ENABLE) {
      saw_enabled = state.enabled;
      if (fault == Fault::kLostEnableReply) return;
      if (fault == Fault::kSignalAfterEnable) {
        ASSERT_EQ(kill(child, SIGTERM), 0);
      }
    }
    if (fault == Fault::kLostEstopReply && request.cmd == JW_CMD_ESTOP) return;
    if (fault == Fault::kWrongMessageId && request.cmd == JW_CMD_IDENTIFY) {
      jw_frame_t reply{};
      ASSERT_EQ(jw_decode_frame(response, size, &reply), JW_RESULT_OK);
      const Bytes payload(reply.payload, reply.payload + reply.payload_len);
      ++request.message_id;
      size =
          jw_encode_response(response, sizeof(response), &request, payload.data(), payload.size());
      ASSERT_GT(size, 0);
    }
    // Exercise serial sync recovery as well as normal JW response decoding.
    const uint8_t noise[] = {0x12, 0x34};
    ASSERT_EQ(write(master, noise, sizeof(noise)), sizeof(noise));
    for (int i = 0; i < size; ++i) ASSERT_EQ(write(master, response + i, 1), 1);
  }

  void Run(std::vector<std::string> flags = {}, bool confirm = true) {
    std::vector<std::string> arguments = {
        binary, "--config=" + directory + "/config.pbtxt", "--board=pty", "--settle_ms=0"};
    if (confirm) arguments.push_back("--confirm_hardware");  // Allocated PTY only.
    arguments.insert(arguments.end(), flags.begin(), flags.end());
    std::vector<char*> argv;
    for (auto& argument : arguments) argv.push_back(argument.data());
    argv.push_back(nullptr);
    posix_spawn_file_actions_t actions;
    ASSERT_EQ(posix_spawn_file_actions_init(&actions), 0);
    const std::string output_path = directory + "/output.txt";
    ASSERT_EQ(posix_spawn_file_actions_addopen(
                  &actions, STDOUT_FILENO, output_path.c_str(), O_CREAT | O_TRUNC | O_WRONLY, 0600),
              0);
    ASSERT_EQ(posix_spawn_file_actions_adddup2(&actions, STDOUT_FILENO, STDERR_FILENO), 0);
    const int spawned =
        posix_spawn(&child, binary.c_str(), &actions, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    ASSERT_EQ(spawned, 0);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    Bytes pending;
    bool exited = false;
    int status = 0;
    while (std::chrono::steady_clock::now() < deadline) {
      const pid_t result = waitpid(child, &status, WNOHANG);
      ASSERT_GE(result, 0);
      if (result == child) {
        child = -1;
        exited = true;
        break;
      }
      pollfd fd{master, POLLIN, 0};
      ASSERT_GE(poll(&fd, 1, 10), 0);
      if (!(fd.revents & POLLIN)) continue;
      uint8_t buffer[128];
      const auto count = read(master, buffer, sizeof(buffer));
      ASSERT_GT(count, 0);
      pending.insert(pending.end(), buffer, buffer + count);
      while (pending.size() >= 2) {
        ASSERT_EQ(pending[0], JW_SYNC_BYTE);
        const size_t length = pending[1] + 4;
        ASSERT_GE(length, JW_FRAME_OVERHEAD);
        ASSERT_LE(length, JW_MAX_FRAME_LEN);
        if (pending.size() < length) break;
        Respond(Bytes(pending.begin(), pending.begin() + length));
        ASSERT_FALSE(HasFatalFailure());
        pending.erase(pending.begin(), pending.begin() + length);
      }
    }
    std::ifstream captured(output_path);
    output.assign(std::istreambuf_iterator<char>(captured), std::istreambuf_iterator<char>());
    ASSERT_TRUE(exited) << "CLI exceeded 5-second test deadline\n" << output;
    ASSERT_TRUE(WIFEXITED(status)) << output;
    exit_code = WEXITSTATUS(status);
  }

  std::vector<std::string> ExerciseFlags() {
    return {"--mode=exercise", "--channel=0", "--allow_enable", "--target_steps=10"};
  }

  int master = -1, slave = -1, exit_code = -1;
  pid_t child = -1;
  std::string binary, directory, output;
  jw_endpoint_t endpoint{};
  JoshuaChannel state{};
  Bytes commands;
  std::vector<uint32_t> sessions;
  Fault fault = Fault::kNone;
  bool saw_enabled = false;
};

TEST_F(SmokeCliTest, DefaultHandshakeAndFreshSessions) {
  Run({"--sessions=2"});
  ASSERT_EQ(exit_code, 0) << output;
  EXPECT_EQ(commands,
            (Bytes{JW_CMD_RESET_SESSION,
                   JW_CMD_IDENTIFY,
                   JW_CMD_ESTOP,
                   JW_CMD_RESET_SESSION,
                   JW_CMD_IDENTIFY,
                   JW_CMD_ESTOP}));
  ASSERT_EQ(sessions.size(), 6);
  EXPECT_NE(sessions[0], sessions[3]);
  EXPECT_FALSE(state.configured);
  EXPECT_FALSE(state.enabled);
  EXPECT_NE(output.find("ESTOP cleanup: OK"), std::string::npos);
}

TEST_F(SmokeCliTest, ConfigureRemainsDisabled) {
  Run({"--mode=configure", "--channel=0"});
  ASSERT_EQ(exit_code, 0) << output;
  EXPECT_EQ(commands,
            (Bytes{JW_CMD_RESET_SESSION,
                   JW_CMD_IDENTIFY,
                   JW_CMD_CONFIGURE_CHANNEL,
                   JW_CMD_GET_FEEDBACK,
                   JW_CMD_ESTOP}));
  EXPECT_TRUE(state.configured);
  EXPECT_FALSE(state.enabled);
}

TEST_F(SmokeCliTest, ExerciseEnablesThenDisablesAndEstops) {
  Run(ExerciseFlags());
  ASSERT_EQ(exit_code, 0) << output;
  EXPECT_EQ(commands,
            (Bytes{JW_CMD_RESET_SESSION,
                   JW_CMD_IDENTIFY,
                   JW_CMD_CONFIGURE_CHANNEL,
                   JW_CMD_GET_FEEDBACK,
                   JW_CMD_SET_TARGET,
                   JW_CMD_ENABLE,
                   JW_CMD_SET_TARGET,
                   JW_CMD_GET_FEEDBACK,
                   JW_CMD_DISABLE,
                   JW_CMD_ESTOP}));
  EXPECT_TRUE(saw_enabled);
  EXPECT_FALSE(state.enabled);
  EXPECT_TRUE(state.estopped);
  EXPECT_FLOAT_EQ(state.target_value, 10);
}

TEST_F(SmokeCliTest, LostEnableReplyFailsAndStillEstops) {
  fault = Fault::kLostEnableReply;
  Run(ExerciseFlags());
  ASSERT_EQ(exit_code, 1) << output;
  EXPECT_TRUE(saw_enabled);
  ASSERT_EQ(commands.size(), 7);
  EXPECT_EQ(commands.back(), JW_CMD_ESTOP);
  EXPECT_FALSE(state.enabled);
  EXPECT_NE(output.find("DEADLINE_EXCEEDED"), std::string::npos);
  EXPECT_NE(output.find("ESTOP cleanup: OK"), std::string::npos);
}

TEST_F(SmokeCliTest, WrongCorrelationFailsBeforeConfiguration) {
  fault = Fault::kWrongMessageId;
  Run(ExerciseFlags());
  ASSERT_EQ(exit_code, 1) << output;
  EXPECT_EQ(commands, (Bytes{JW_CMD_RESET_SESSION, JW_CMD_IDENTIFY, JW_CMD_ESTOP}));
  EXPECT_FALSE(state.configured);
  EXPECT_NE(output.find("DATA_LOSS"), std::string::npos);
}

TEST_F(SmokeCliTest, UnconfirmedCleanupReportsUnknownState) {
  fault = Fault::kLostEstopReply;
  Run();
  ASSERT_EQ(exit_code, 1) << output;
  EXPECT_EQ(commands, (Bytes{JW_CMD_RESET_SESSION, JW_CMD_IDENTIFY, JW_CMD_ESTOP}));
  EXPECT_NE(output.find("ESTOP unconfirmed, hardware state unknown"), std::string::npos);
}

TEST_F(SmokeCliTest, SigtermAfterEnableStillEstops) {
  fault = Fault::kSignalAfterEnable;
  Run(ExerciseFlags());
  ASSERT_EQ(exit_code, 1) << output;
  EXPECT_TRUE(saw_enabled);
  ASSERT_EQ(commands.size(), 7);
  EXPECT_EQ(commands.back(), JW_CMD_ESTOP);
  EXPECT_FALSE(state.enabled);
  EXPECT_NE(output.find("CANCELLED"), std::string::npos);
}

TEST_F(SmokeCliTest, DryRunDoesNotOpenPort) {
  Run({"--dry_run"}, false);
  ASSERT_EQ(exit_code, 0) << output;
  EXPECT_TRUE(commands.empty());
  EXPECT_NE(output.find("DRY RUN: no hardware opened"), std::string::npos);
  EXPECT_EQ(output.find("Opening selected serial port"), std::string::npos);
}

TEST_F(SmokeCliTest, MissingConfirmationDoesNotOpenPort) {
  Run({}, false);
  ASSERT_EQ(exit_code, 1) << output;
  EXPECT_TRUE(commands.empty());
  EXPECT_NE(output.find("No port opened"), std::string::npos);
  EXPECT_EQ(output.find("Opening selected serial port"), std::string::npos);
}

TEST_F(SmokeCliTest, MissingEnableOptInDoesNotOpenPort) {
  Run({"--mode=exercise", "--channel=0", "--target_steps=10"});
  ASSERT_EQ(exit_code, 1) << output;
  EXPECT_TRUE(commands.empty());
  EXPECT_NE(output.find("INVALID_ARGUMENT"), std::string::npos);
  EXPECT_EQ(output.find("Opening selected serial port"), std::string::npos);
}
}  // namespace
