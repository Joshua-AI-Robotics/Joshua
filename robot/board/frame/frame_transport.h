#pragma once

#include "robot/comm/interfaces/legacy_message_transport.h"

namespace robot::board {

// Board-facing name for the generic atomic message capability.
using FrameTransport = robot::comm::LegacyMessageTransport;

}  // namespace robot::board
