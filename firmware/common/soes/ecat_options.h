// Fixed JW layout-v2 SOES configuration. Board SII must use these same settings.
#pragma once
#include "firmware/common/joshua_wire_ethercat.h"
#define USE_FOE 0
#define USE_EOE 0
#define MBXSIZE 512
#define MBXSIZEBOOT 512
#define MBXBUFFERS 3
#define MBX0_sma 0x1000
#define MBX1_sma 0x1200
#define SM2_sma 0x1400
#define SM3_sma 0x1a00
#define MAX_RXPDO_SIZE JWEC_PDO_SIZE
#define MAX_TXPDO_SIZE JWEC_PDO_SIZE
#define MAX_MAPPINGS_SM2 (JWEC_PDO_SIZE / 4)
#define MAX_MAPPINGS_SM3 (JWEC_PDO_SIZE / 4)
