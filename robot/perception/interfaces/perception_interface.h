#pragma once

#include <memory>
#include <string>
#include "robot/perception/proto/perception_packet.pb.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace robot::perception {

class PerceptionInterface {
public:
    virtual ~PerceptionInterface() = default;

    virtual std::string GetId() = 0;
    virtual absl::StatusOr<robot::perception::PerceptionPacket> GetData() = 0;
};

}  // namespace robot::perception