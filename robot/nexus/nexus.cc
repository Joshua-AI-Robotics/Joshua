#include "robot/nexus/nexus.h"

namespace robot::nexus {

Nexus::Nexus(const int& trigger_frequency):
trigger_frequency_(trigger_frequency)
{
    LOG(INFO) << "Nexus construted.";
}

Nexus::~Nexus(){}

bool Nexus::Init(){}

bool Nexus::Register(){}

}