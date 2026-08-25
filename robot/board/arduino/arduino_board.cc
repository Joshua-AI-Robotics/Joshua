#include "robot/board/arduino/arduino_board.h"

#include <chrono>
#include <thread>

#include "utils/status_macros.h"

namespace robot::board {

absl::StatusOr<std::shared_ptr<FrameTransport>> ArduinoBoard::CreateTransport(
    const robot::comm::Comm& comm) const {
  ABSL_ASSIGN_OR_RETURN(auto transport, JoshuaWireBoard::CreateTransport(comm));
  // Official Uno R3: opening /dev/ttyACM* pulses DTR via the 16U2 and the
  // 328P spends ~1s in optiboot before setup(). IDENTIFY's AtomicRead is
  // ~20ms and loses that race. CH340 clones on /dev/ttyUSB* often behave
  // the same. FakeFrameTransport tests never enter this method.
  std::this_thread::sleep_for(std::chrono::milliseconds(2000));
  return transport;
}

}  // namespace robot::board
