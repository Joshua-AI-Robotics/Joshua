#include "joshua_peripheral_device.h"

#include "joshua_stepdir_device.h"
namespace {
jw_runtime_function_t functions[32];
jw_resource_claim_t resources[96];
jw_peripheral_group_t group;
void Safety(void*, int) {
  jw_peripheral_group_reset(&group);
}
}  // namespace
int JoshuaPeripheralDeviceInit(jw_peripheral_ops_t ops) {
  if (!ops.validate || !ops.configure || !ops.execute || !ops.release || !ops.bus_allowed) return 0;
  if (!StepDirDeviceExtend(
          functions, 32, resources, 96, {nullptr, Safety, nullptr, nullptr, nullptr}))
    return 0;
  auto* runtime = StepDirDeviceRuntime();
  const auto count = runtime->identity.function_count;
  if (count + JW_PERIPHERAL_SLOTS > 32) return 0;
  jw_peripheral_group_init(&group, runtime, functions + count, resources, 96, ops);
  runtime->identity.function_count += JW_PERIPHERAL_SLOTS;
  // Teensy Wire uses fixed per-phase timeouts (50ms plus idle acquisition).
  // Advertise a conservative software budget; physical timing is unqualified.
  runtime->identity.max_handler_us = 200000;
  return 1;
}
