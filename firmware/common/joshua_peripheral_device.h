#pragma once
#include "joshua_peripheral_handler.h"
// Setup-time composition on the existing STEP/DIR runtime and resource table.
// The supplied backend never owns another transport or session.
int JoshuaPeripheralDeviceInit(jw_peripheral_ops_t);
#ifdef JOSHUA_PERIPHERAL_REFERENCE
int JoshuaArduinoPeripheralsInit();
#endif
