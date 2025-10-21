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

    ~CommFactory() = default;
    CommFactory(const CommFactory&) = delete;
    CommFactory& operator=(const CommFactory&) = delete;
    CommFactory(CommFactory&&) = default;
    CommFactory& operator=(CommFactory&&) = default;
private:
    CommFactory() = default;
    
};
}
