#pragma once

#include "robot/nexus/proto/nexus_packet.pb.h"
#include <glog/logging.h>

// Abstract motor interface.
namespace robot::actuation{
class MotorInterface{
  public:
    MotorInterface() = default;
    virtual ~MotorInterface() = default;
    virtual void SetSpeed(float value) = 0;
    virtual void SetPosition(float angle) = 0;
    virtual void SetTorque(float torque) = 0;
    virtual void SetAction(robot::nexus::NexusActionPacket action_packet) = 0;
    virtual float GetPosition() = 0;
    virtual void SetMiddlePosition(){ LOG(WARNING) << "SetMiddlePosition not implemented.";};
    virtual void SetIdlePosition(){ LOG(WARNING) << "SetIdlePosition not implemented.";};
};
}