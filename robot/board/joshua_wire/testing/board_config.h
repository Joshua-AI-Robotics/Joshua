#pragma once

#include "firmware/common/joshua_wire.h"
#include "robot/board/proto/board.pb.h"

namespace robot::board {
inline Board MakeStepDirBoardConfig(BoardType type) {
  Board board;
  board.set_name("stepdir");
  board.set_board_type(type);
  board.set_protocol(JOSHUA_WIRE);
  board.mutable_firmware()->set_min_proto_version(JW_PROTO_VERSION);
  auto* comm = board.mutable_comm();
  comm->set_comm_type(robot::comm::SERIAL);
  comm->set_transport_type(robot::comm::MESSAGE);
  auto* serial = comm->mutable_serial_config();
  serial->set_port("never-open-this-test-port");
  serial->set_baudrate(115200);
  serial->set_message_adapter(robot::comm::JOSHUA_WIRE);
  auto* channel = board.add_channels();
  channel->set_index(0);
  channel->set_drive(STEP_DIR);
  auto* config = channel->mutable_step_dir();
  config->set_step_pin(2);
  config->set_dir_pin(3);
  config->set_enable_pin(4);
  config->set_enable_active_low(true);
  config->set_max_pulse_rate_hz(4000);
  return board;
}
}  // namespace robot::board
