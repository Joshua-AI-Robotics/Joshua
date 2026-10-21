# Shared firmware protocol

`joshua_wire` implements JoshuaWire **0.2.0 / wire revision 4**. The normative
[protocol contract](../../docs/JOSHUA_WIRE.md) defines mixed-device discovery,
versioned profiles, one session owner across all functions/transports, bounded
callbacks and scheduling, shared resource validation, observable safety state,
freshness/error semantics and the required stacked integration tests.

The core (`joshua_wire_commands`) contains discovery, common replies, profile
selectors and board/function state schemas. It does not import actuator, sensor
or I/O types. `joshua_wire_profiles` contains initial actuator, scalar sensor and
I/O codecs. `jw_drive_t` remains actuator-only metadata. A new profile or board
needs no framing/session change or board enum. Vendor/product IDs are informational.

IDENTIFY advertises limits, supported protocol/planes, boot/generation identity
and safety scope. DISCOVER pages at most four stable function descriptors; the
function ID space supports 255 functions. Device IDs group multifunction devices.
DESCRIBE_FUNCTION supplies profile metadata; GET_DEPENDENCIES enumerates shared
shutdown effects. GET_STATUS is queryable after lost events/reconnect and separates
firmware stop progress, physical verification support and recovery requirements.

One `jw_endpoint_t` per MCU dispatches every transport through the same session.
Callbacks must be bounded and return a common status/execution header plus data.
Slow measurements use background acquisition and cached typed samples, NOT_READY
or BUSY. Handler failure produces cached OUTCOME_UNKNOWN; exact retries cannot
repeat side effects. `jw_resource_replace` validates ownership before registry
changes; board backends retain responsibility for staging/rolling back hardware.

Revision 4 is incompatible with earlier new-codec revisions. Frames remain
96 bytes; EtherCAT layout 2's mailbox/PDO slots remain 108/112 bytes, with the
advertised protocol range updated to revision 4 by the adapter/firmware stack.
All Joshua-authored host MCU bindings and firmware images use this revision;
the previous codec and host implementation have been removed. Teensy/ESP32 share
`joshua_stepdir_device`, the physical STEP/DIR backend and incremental serial
framing. The AM243 UART overlay uses the current endpoint with its existing
software-only actuator backend. Firmware builds never flash devices.

Hardware-free tests include golden payload/frame bytes, bounded C codecs,
session retry/reset/reboot behavior and a twelve-function mixed-device fake.
The fake coordinator/acquisition/scheduler illustrates protocol requirements;
production concurrency/deadline/rollback tests belong to the H/F/T stacked PRs
identified in the protocol contract and
[board/comm migration plan](../../docs/BOARD_COMM_SEPARATION_PLAN.md).
