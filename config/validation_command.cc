#include "config/validation_command.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/magic.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <unistd.h>

#ifndef CONFIGFS_MAGIC
#define CONFIGFS_MAGIC 0x62656570
#endif

#include <algorithm>
#include <cstdio>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "config/config_utils.h"
#include "config/validation.h"
#include "google/protobuf/struct.pb.h"
#include "google/protobuf/util/json_util.h"

namespace config {
namespace {

constexpr size_t kConfigLimit = 10 * 1024 * 1024;
constexpr size_t kMessageLimit = 8 * 1024;
constexpr size_t kReportLimit = 64 * 1024;
constexpr char kLimitations[] = "Implementation support and hardware readiness were not checked.";

struct FdCloser {
  void operator()(int* fd) const {
    if (fd) {
      close(*fd);
      delete fd;
    }
  }
};
using UniqueFd = std::unique_ptr<int, FdCloser>;

struct Report {
  std::optional<std::string> path;
  std::string stage;
  std::string result;
  absl::StatusCode status;
  std::string message;
  int exit_code;
};

bool IsUtf8(const std::string& text) {
  for (size_t i = 0; i < text.size();) {
    const unsigned char c = text[i];
    if (c < 0x80) {
      ++i;
      continue;
    }
    int count = c >= 0xc2 && c <= 0xdf   ? 2
                : c >= 0xe0 && c <= 0xef ? 3
                : c >= 0xf0 && c <= 0xf4 ? 4
                                         : 0;
    if (count == 0 || i + count > text.size()) return false;
    for (int j = 1; j < count; ++j) {
      if ((static_cast<unsigned char>(text[i + j]) & 0xc0) != 0x80) return false;
    }
    const unsigned char second = text[i + 1];
    if ((c == 0xe0 && second < 0xa0) || (c == 0xed && second >= 0xa0) ||
        (c == 0xf0 && second < 0x90) || (c == 0xf4 && second >= 0x90))
      return false;
    i += count;
  }
  return true;
}

std::string SafeMessage(const std::string& raw) {
  std::string result;
  result.reserve(std::min(raw.size(), kMessageLimit));
  const char hex[] = "0123456789ABCDEF";
  bool truncated = false;
  for (size_t i = 0; i < raw.size(); ++i) {
    const unsigned char c = raw[i];
    std::string next;
    if (c < 32 || c >= 127) {
      next = "\\x";
      next += hex[c >> 4];
      next += hex[c & 15];
    } else {
      next = static_cast<char>(c);
    }
    if (result.size() + next.size() > kMessageLimit - 15) {
      truncated = true;
      break;
    }
    result += next;
  }
  if (truncated) result += "... [truncated]";
  return result;
}

std::string JsonQuote(const std::string& text) {
  google::protobuf::Value value;
  value.set_string_value(text);
  std::string encoded;
  auto status = google::protobuf::util::MessageToJsonString(value, &encoded);
  return status.ok() ? encoded : "\"JSON encoding failed\"";
}

std::string Encode(const Report& report) {
  std::string encoded = "{\"schema_version\":1,\"scope\":\"config_integrity\",\"config_path\":";
  encoded += report.path ? JsonQuote(*report.path) : "null";
  encoded += ",\"stage\":" + JsonQuote(report.stage);
  encoded += ",\"result\":" + JsonQuote(report.result);
  encoded += ",\"status_code\":" + JsonQuote(absl::StatusCodeToString(report.status));
  encoded += ",\"message\":" + JsonQuote(SafeMessage(report.message));
  encoded += ",\"limitations\":[" + JsonQuote(kLimitations) + "]}\n";
  return encoded;
}

absl::Status InputStatus(int error) {
  switch (error) {
    case ENOENT:
    case ENOTDIR:
      return absl::NotFoundError("Configuration file was not found.");
    case EACCES:
    case EPERM:
      return absl::PermissionDeniedError("Configuration file is not accessible.");
    default:
      return absl::InternalError("Failed to access configuration file.");
  }
}

absl::StatusOr<std::string> OpenPinnedPath(const std::string& path,
                                           UniqueFd* pin,
                                           UniqueFd* content) {
  int descriptor = open(path.c_str(), O_PATH | O_CLOEXEC);
  if (descriptor < 0) return InputStatus(errno);
  pin->reset(new int(descriptor));
  struct stat pinned_stat;
  if (fstat(descriptor, &pinned_stat) != 0) return absl::InternalError("Failed to stat input.");
  if (!S_ISREG(pinned_stat.st_mode)) {
    return absl::InvalidArgumentError("Configuration input must be a regular file.");
  }
  struct statfs fs;
  if (fstatfs(descriptor, &fs) != 0) return absl::InternalError("Failed to inspect filesystem.");
  switch (fs.f_type) {
    case PROC_SUPER_MAGIC:
    case SYSFS_MAGIC:
    case DEBUGFS_MAGIC:
    case TRACEFS_MAGIC:
    case CONFIGFS_MAGIC:
      return absl::InvalidArgumentError("Configuration input is on a pseudo-filesystem.");
  }
  if (pinned_stat.st_size > static_cast<off_t>(kConfigLimit)) {
    return absl::ResourceExhaustedError("Configuration input exceeds 10 MiB.");
  }
  const std::string pinned_path = "/proc/self/fd/" + std::to_string(descriptor);
  int reopened = open(pinned_path.c_str(), O_RDONLY | O_CLOEXEC | O_NONBLOCK);
  if (reopened < 0) return InputStatus(errno);
  content->reset(new int(reopened));
  struct stat content_stat;
  if (fstat(reopened, &content_stat) != 0 || !S_ISREG(content_stat.st_mode) ||
      content_stat.st_dev != pinned_stat.st_dev || content_stat.st_ino != pinned_stat.st_ino)
    return absl::InternalError("Pinned input changed unexpectedly.");
  return pinned_path;
}

Report Evaluate(const std::vector<std::string>& args) {
  std::optional<std::string> path;
  bool bad_args = false;
  for (size_t i = 0; i < args.size(); ++i) {
    const auto& arg = args[i];
    if (arg == "--config") {
      if (path || i + 1 >= args.size()) {
        bad_args = true;
      } else {
        path = args[++i];
      }
    } else if (arg.rfind("--config=", 0) == 0) {
      if (path)
        bad_args = true;
      else
        path = arg.substr(9);
    } else {
      bad_args = true;
    }
  }
  if (!path || path->empty() || bad_args) {
    return {bad_args ? std::nullopt : path,
            "arguments",
            "not_run",
            absl::StatusCode::kInvalidArgument,
            "Require exactly one --config PATH.",
            2};
  }
  if (!IsUtf8(*path)) {
    return {std::nullopt,
            "input",
            "not_run",
            absl::StatusCode::kInvalidArgument,
            "Configuration path must be UTF-8.",
            1};
  }
  UniqueFd pin, content;
  auto pinned_path = OpenPinnedPath(*path, &pin, &content);
  if (!pinned_path.ok()) {
    return {path,
            "input",
            "not_run",
            pinned_path.status().code(),
            std::string(pinned_path.status().message()),
            pinned_path.status().code() == absl::StatusCode::kInternal ? 3 : 1};
  }
  auto parsed = config_util::LoadConfig(*pinned_path, kConfigLimit);
  if (!parsed.ok()) {
    if (parsed.status().code() == absl::StatusCode::kResourceExhausted ||
        parsed.status().code() == absl::StatusCode::kInternal) {
      return {path,
              "input",
              "not_run",
              parsed.status().code(),
              std::string(parsed.status().message()),
              parsed.status().code() == absl::StatusCode::kInternal ? 3 : 1};
    }
    return {
        path, "load", "invalid", parsed.status().code(), std::string(parsed.status().message()), 1};
  }
  auto status = ValidateConfig(*parsed);
  return {path,
          "integrity",
          status.ok() ? "valid" : "invalid",
          status.code(),
          status.ok() ? "Configuration integrity is valid." : std::string(status.message()),
          status.ok() ? 0 : 1};
}

int WriteReport(const Report& report, std::ostream& output) {
  std::string encoded = Encode(report);
  const bool oversized = encoded.size() > kReportLimit;
  if (oversized) {
    encoded = Encode({std::nullopt,
                      "input",
                      "not_run",
                      absl::StatusCode::kInternal,
                      "Validation report exceeded the output limit.",
                      3});
  }
  output << encoded;
  output.flush();
  return output.good() ? (oversized ? 3 : report.exit_code) : 3;
}
}  // namespace

int RunValidationCommand(const std::vector<std::string>& args, std::ostream& output) {
  if (args.size() == 1 && args[0] == "--help") {
    output << "Usage: joshua_validate_config --config PATH\n";
    output.flush();
    return output.good() ? 0 : 3;
  }
  try {
    return WriteReport(Evaluate(args), output);
  } catch (...) {
    return WriteReport({std::nullopt,
                        "input",
                        "not_run",
                        absl::StatusCode::kInternal,
                        "Unexpected validation failure.",
                        3},
                       output);
  }
}
}  // namespace config
