// Test-only TI EtherCAT API surface used by joshua_ethercat_ti.c. These stubs
// register real production callbacks without importing the proprietary stack.
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct {
  uint32_t unused;
} EC_API_SLV_SHandle_t;
typedef struct {
  uint32_t unused;
} EC_API_SLV_Pdo_t;
typedef struct {
  uint32_t unused;
} EC_API_SLV_SCoE_ObjEntry_t;
typedef enum { EC_API_SLV_eESM_op = 8 } EC_API_SLV_EEsmState_t;
typedef uint8_t (*EC_API_SLV_CBObjRead_t)(void*, uint16_t, uint8_t, uint32_t, uint16_t*, uint8_t);
typedef EC_API_SLV_CBObjRead_t EC_API_SLV_CBObjWrite_t;
typedef void (*EC_API_SLV_CBStopOutputHandler_t)(void*);

#ifdef __cplusplus
extern "C" {
#endif
EC_API_SLV_EEsmState_t EC_API_SLV_getState(void);
uint32_t EC_API_SLV_getOutputProcDataLength(EC_API_SLV_SHandle_t*);
uint32_t EC_API_SLV_getInputProcDataLength(EC_API_SLV_SHandle_t*);
uint32_t EC_API_SLV_getOutputData(EC_API_SLV_SHandle_t*, uint32_t, uint8_t*);
uint32_t EC_API_SLV_setInputData(EC_API_SLV_SHandle_t*, uint32_t, uint8_t*);
uint32_t EC_API_SLV_setProductCode(EC_API_SLV_SHandle_t*, uint32_t);
uint32_t EC_API_SLV_setRevisionNumber(EC_API_SLV_SHandle_t*, uint32_t);
uint32_t EC_API_SLV_setProductName(EC_API_SLV_SHandle_t*, char*);
uint32_t EC_API_SLV_CoE_odAddVariable(EC_API_SLV_SHandle_t*,
                                      uint16_t,
                                      char*,
                                      uint16_t,
                                      uint16_t,
                                      uint16_t,
                                      EC_API_SLV_CBObjRead_t,
                                      void*,
                                      EC_API_SLV_CBObjWrite_t,
                                      void*);
uint32_t EC_API_SLV_CoE_odAddArray(EC_API_SLV_SHandle_t*,
                                   uint16_t,
                                   char*,
                                   uint8_t,
                                   uint16_t,
                                   uint16_t,
                                   uint16_t,
                                   EC_API_SLV_CBObjRead_t,
                                   void*,
                                   EC_API_SLV_CBObjWrite_t,
                                   void*);
uint32_t EC_API_SLV_PDO_create(EC_API_SLV_SHandle_t*, char*, uint16_t, EC_API_SLV_Pdo_t**);
uint32_t EC_API_SLV_CoE_getObjectEntry(EC_API_SLV_SHandle_t*,
                                       uint16_t,
                                       uint8_t,
                                       EC_API_SLV_SCoE_ObjEntry_t**);
uint32_t EC_API_SLV_PDO_createEntry(EC_API_SLV_SHandle_t*,
                                    EC_API_SLV_Pdo_t*,
                                    char*,
                                    EC_API_SLV_SCoE_ObjEntry_t*);
uint32_t EC_API_SLV_PDO_setFixed(EC_API_SLV_SHandle_t*, EC_API_SLV_Pdo_t*, bool);
uint32_t EC_API_SLV_PDO_setAssignment(EC_API_SLV_SHandle_t*, bool);
void EC_API_SLV_cbRegisterStopOuputHandler(EC_API_SLV_SHandle_t*,
                                           void*,
                                           EC_API_SLV_CBStopOutputHandler_t);
#ifdef __cplusplus
}
#endif
