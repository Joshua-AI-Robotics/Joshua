#pragma once

#include "absl/status/status.h"
#include "absl/status/statusor.h"

#define ABSL_RETURN_IF_ERROR(expr)       \
  do {                                   \
    const absl::Status _status = (expr); \
    if (!_status.ok()) return _status;   \
  } while (0)

#define ABSL_ASSIGN_OR_RETURN_IMPL(statusor, lhs, rexpr) \
  auto statusor = (rexpr);                               \
  if (!statusor.ok()) return statusor.status();          \
  lhs = std::move(statusor).value()

#define ABSL_ASSIGN_OR_RETURN_CONCAT(x, y) x##y
#define ABSL_ASSIGN_OR_RETURN_NAME(x, y) ABSL_ASSIGN_OR_RETURN_CONCAT(x, y)

#define ABSL_ASSIGN_OR_RETURN(lhs, rexpr) \
  ABSL_ASSIGN_OR_RETURN_IMPL(ABSL_ASSIGN_OR_RETURN_NAME(_status_or_, __LINE__), lhs, rexpr)

