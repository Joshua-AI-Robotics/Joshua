// Private newline-delimited JSON IPC, not an implementation of MCP.
#include <poll.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <csignal>
#include <iostream>
#include <thread>

#include "config/config_utils.h"
#include "mhs/runtime.h"
#include "google/protobuf/util/json_util.h"
#include "robot/board/teensy/teensy_board.h"

namespace {
volatile std::sig_atomic_t shutdown_requested = 0;
void RequestShutdown(int) {
  shutdown_requested = 1;
}
}  // namespace

int main(int argc, char** argv) {
  google::InitGoogleLogging(argv[0]);
  if (argc != 2 && (argc != 3 || std::string(argv[2]) != "--hardware-and-reference-confirmed")) {
    std::cerr << "Usage: executor CONFIG [--hardware-and-reference-confirmed]\n";
    return 2;
  }
  // A broken adapter pipe must unwind through Runtime's disabling destructor.
  std::signal(SIGPIPE, SIG_IGN);
  std::signal(SIGTERM, RequestShutdown);
  std::signal(SIGINT, RequestShutdown);
  auto config = config::config_util::LoadConfig(argv[1]);
  if (!config.ok()) {
    std::cerr << config.status() << '\n';
    return 2;
  }
  auto device = mhs::ResolveDevice(*config);
  if (!device.ok()) {
    std::cerr << device.status() << '\n';
    return 2;
  }
  robot::board::TeensyBoard board;
  mhs::Runtime runtime(*device);
  if (argc == 3) {
    auto status = board.Init(device->board);
    if (!status.ok()) {
      std::cerr << status << '\n';
      return 1;
    }
    auto channel = board.OpenChannel(device->actuator.channel());
    if (!channel.ok()) {
      std::cerr << channel.status() << '\n';
      return 1;
    }
    status = runtime.Attach(*channel);
    if (!status.ok()) {
      std::cerr << status << '\n';
      return 1;
    }
  }
  std::atomic<bool> done{false};
  std::thread monitor([&] {
    while (!done.load()) {
      runtime.Poll();
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
  });
  std::string pending;
  while (!shutdown_requested) {
    pollfd input{STDIN_FILENO, POLLIN, 0};
    const int ready = poll(&input, 1, 100);
    if (ready < 0) {
      if (errno == EINTR) continue;
      break;
    }
    if (ready == 0) continue;
    char buffer[4096];
    const auto count = read(STDIN_FILENO, buffer, sizeof(buffer));
    if (count <= 0) break;
    pending.append(buffer, count);
    if (pending.size() > 65536) {
      std::cerr << "IPC request too large\n";
      break;
    }
    size_t newline;
    while ((newline = pending.find('\n')) != std::string::npos && !shutdown_requested) {
      const auto line = pending.substr(0, newline);
      pending.erase(0, newline + 1);
      google::protobuf::Struct request, response;
      const auto parsed = google::protobuf::util::JsonStringToMessage(line, &request);
      if (!parsed.ok()) {
        (*response.mutable_fields())["error"].set_string_value("Invalid request JSON object");
      } else {
        response = runtime.Handle(request);
      }
      std::string json;
      const auto encoded = google::protobuf::util::MessageToJsonString(response, &json);
      if (!encoded.ok()) json = "{\"error\":\"Response encoding failed\"}";
      std::cout << json << std::endl;
      if (!std::cout) {
        shutdown_requested = 1;
        break;
      }
    }
  }
  done.store(true);
  monitor.join();
  return 0;
}
