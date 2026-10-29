// TI SDK 09 bridge for the explicitly selected JW EtherCAT-only artifact.
// Installs the object dictionary and fixed PDO mapping; no automatic flashing.
#pragma once
#include "ecSlvApi.h"

#ifdef __cplusplus
extern "C" {
#endif
// Configure once at startup, before any stack callbacks/watchdog can run.
// This adapter owns all channel/profile access on one R5 core (IRQ exclusion;
// FIQ and other cores must not enter). The safety task preempts SDK task work;
// a running bounded critical section completes before stop can take ownership.
uint32_t JoshuaEthercatConfigure(EC_API_SLV_SHandle_t* slave);
// Snapshot SDK process data outside the owner critical section, then dispatch
// only if no stop occurred since admission. Publish a complete response image.
void JoshuaEthercatRun(EC_API_SLV_SHandle_t* slave);
#ifdef __cplusplus
}
#endif
