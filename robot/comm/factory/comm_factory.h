#pragma once

#include "robot/comm/proto/comm.pb.h"
#include "robot/comm/serial/serial.h"
#include <map>
#include <memory>
#include <string>
#include <mutex>
#include <utility> 
#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace robot::comm {

class CommFactory {
public:
    static absl::StatusOr<std::shared_ptr<Serial>> CreateSerial(const robot::comm::Comm& comm);
private:
    CommFactory() = default;
    ~CommFactory();
};

} 