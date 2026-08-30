TI_PROJECT_DIR ?= $(INDUSTRIAL_COMMUNICATIONS_SDK_PATH)/examples/industrial_comms/ethercat_slave_demo/device_profiles/401_simple/am243x-lp/r5fss0-0_freertos/ti-arm-clang
JOSHUA_ROOT ?= $(abspath ../../..)
JOSHUA_PATCHED_SOURCE_DIR ?= $(CURDIR)/patched

include $(TI_PROJECT_DIR)/makefile

JOSHUA_SOURCE_DIR := $(JOSHUA_ROOT)/firmware/am243/joshua_dual_transport_v1/src
JOSHUA_COMMON_DIR := $(JOSHUA_ROOT)/firmware/common
TI_PROFILE_DIR := $(INDUSTRIAL_COMMUNICATIONS_SDK_PATH)/examples/industrial_comms/ethercat_slave_demo/device_profiles/401_simple
TI_DEMO_DIR := $(INDUSTRIAL_COMMUNICATIONS_SDK_PATH)/examples/industrial_comms/ethercat_slave_demo

FILES_PATH := \
	$(JOSHUA_PATCHED_SOURCE_DIR) \
	$(JOSHUA_SOURCE_DIR) \
	$(JOSHUA_COMMON_DIR) \
	$(TI_PROFILE_DIR)/am243x-lp/r5fss0-0_freertos \
	$(TI_PROFILE_DIR) \
	$(TI_DEMO_DIR)/common \
	$(TI_DEMO_DIR)/common/board/am243lp \
	$(TI_DEMO_DIR)/common/board/am243lp/freertos \
	$(TI_DEMO_DIR)/common/os \
	$(TI_DEMO_DIR)/common/os/freertos \
	$(INDUSTRIAL_COMMUNICATIONS_SDK_PATH)/examples/industrial_comms/custom_phy/src \
	$(INDUSTRIAL_COMMUNICATIONS_SDK_PATH)/examples/industrial_comms/custom_phy/inc \
	generated

INCLUDES += -I$(JOSHUA_SOURCE_DIR) -I$(JOSHUA_COMMON_DIR)

OBJS += joshua_serial_task.obj joshua_wire_v1.obj
DEPS += joshua_serial_task.d joshua_wire_v1.d

vpath %.c
vpath %.c $(FILES_PATH)

$(OUTNAME): joshua_serial_task.obj joshua_wire_v1.obj
