#pragma once

#include <vector>
#include <string>
#include <glog/logging.h>
#include <memory>

// Abstract action interface - high-level interface for all action components.
namespace robot::action{
class ActionInterface{
  public:
    ActionInterface() = default;
    virtual ~ActionInterface() = default;
    
    // Common interface methods for all action components
    virtual std::string GetId() = 0;
    virtual void SetAction() = 0;
};
}