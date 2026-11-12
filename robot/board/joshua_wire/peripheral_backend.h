#pragma once
#include <functional>

#include "firmware/common/joshua_wire_peripheral.h"
#include "robot/board/host_gpio/peripheral_backend.h"
#include "robot/board/interfaces/digital_pin.h"
namespace robot::board {
using PeripheralExchange = std::function<absl::StatusOr<std::vector<uint8_t>>(
    uint8_t, uint8_t, const std::vector<uint8_t>&)>;
absl::StatusOr<jw_peripheral_config_t> PeripheralConfig(const PeripheralResource&);
std::shared_ptr<PeripheralBackend> MakeJoshuaWirePeripheralBackend(PeripheralExchange);
absl::StatusOr<std::shared_ptr<DigitalPin>> MakeJoshuaWireDigitalPin(const PeripheralResource&,
                                                                     const PinProfile&,
                                                                     PeripheralExchange);
}  // namespace robot::board
