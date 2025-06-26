#pragma once

#include <vector>
#include <string>
#include "robot/nexus/proto/nexus_packet.pb.h"
#include <glog/logging.h>
#include <memory>

// Abstract motor interface.
namespace robot::actuation{
class MotorInterface{
  public:
    MotorInterface() = default;
    virtual ~MotorInterface() = default;
    virtual void SetSpeed(float value) = 0;
    virtual void SetPosition(float angle) = 0;
    virtual void SetTorque(float torque) = 0;
    virtual void SetAction(std::unique_ptr<::robot::nexus::NexusActionPacket> action_packet) = 0;
    virtual std::string GetId() = 0;
    virtual float GetPosition() = 0;
    virtual void SetMiddlePosition(){ LOG(WARNING) << "SetMiddlePosition not implemented.";};
    virtual void SetIdlePosition(){ LOG(WARNING) << "SetIdlePosition not implemented.";};
    // TODO: Add this in the config. (e.g. what's the idle position, what's the torque, speed, etc.)
    virtual void GracefulShutdown(){ LOG(WARNING) << "GracefulShutdown not implemented.";}; 
};
}