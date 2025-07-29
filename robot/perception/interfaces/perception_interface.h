#pragma once

#include <any>
#include <memory>

namespace robot::perception {

class PerceptionInterface {
public:
    virtual ~PerceptionInterface() = default;

    virtual std::string GetId() = 0;
    virtual std::any GetData() = 0;
};

}  // namespace robot::perception