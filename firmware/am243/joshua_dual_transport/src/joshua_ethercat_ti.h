// TI SDK 09 bridge for the explicitly selected JW EtherCAT-only artifact.
// Installs the object dictionary and fixed PDO mapping; no automatic flashing.
#pragma once
#include "ecSlvApi.h"

uint32_t JoshuaEthercatConfigure(EC_API_SLV_SHandle_t* slave);
void JoshuaEthercatRun(EC_API_SLV_SHandle_t* slave);
