#include "robot/board/esp32/esp32_board.h"

#include <chrono>
#include <thread>

#include "utils/status_macros.h"

namespace robot::board {

// Most ESP32 dev boards (this one included: CP2102 bridge, confirmed via
// lsusb) wire the USB-serial bridge's DTR/RTS lines into an RC circuit on
// EN/GPIO0 — the same auto-reset-into-bootloader trick esptool.py uses to
// flash without a manual BOOT-button press. The side effect: simply
// *opening* the port (which asserts DTR) reboots the board, so the very
// first exchange on a freshly-opened connection races the ESP32's
// bootloader boot-log output (printed at a different baud, read as noise)
// and Arduino setup(). Teensy 4.1's native-USB CDC has no such reset-on-
// open behavior, which is why this settle delay lives here and not in
// JoshuaWireBoard. Serial::AtomicRead already flushes the input buffer
// immediately before writing each request, so once boot has actually
// finished, no leftover boot-log bytes remain to corrupt IDENTIFY's
// response — this delay only needs to outlast the boot itself.
constexpr std::chrono::milliseconds kPostResetSettleDelay(2000);

absl::StatusOr<std::shared_ptr<FrameTransport>> Esp32Board::CreateTransport(
    const robot::comm::Comm& comm) const {
  ABSL_ASSIGN_OR_RETURN(auto transport, JoshuaWireBoard::CreateTransport(comm));
  std::this_thread::sleep_for(kPostResetSettleDelay);
  return transport;
}

}  // namespace robot::board
