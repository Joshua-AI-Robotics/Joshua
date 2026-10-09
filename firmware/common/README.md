# Shared firmware protocol

The `joshua_wire` codec implements wire revision 2: CRC-protected complete
frames carry session and message IDs. Pure C firmware session and endpoint
helpers enforce RESET, duplicate handling and command dispatch without device
I/O. Semantic payloads live in `joshua_wire_commands`; the EtherCAT envelope
layout is defined separately. Existing board consumers still select JW1 until
the shared engine and firmware migration stages land. Native golden-frame and
session tests do not open hardware.

The [generic-device draft](../../docs/JOSHUA_WIRE_GENERIC_DEVICE_PROPOSAL.md)
proposes mixed actuator/sensor/I/O discovery and observable state. Its executable
reference is a separate test-only target under `firmware/reference/`; existing
firmware does not select it.
