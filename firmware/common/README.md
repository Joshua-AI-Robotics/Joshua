# Shared firmware protocol

The `joshua_wire` codec implements wire revision 2: CRC-protected complete
frames carry session and message IDs. Pure C firmware session and endpoint
helpers enforce RESET, duplicate handling and command dispatch without device
I/O. Semantic payloads live in `joshua_wire_commands`; the EtherCAT envelope
layout is defined separately. Teensy, ESP32 and AM243 UART bindings now use the shared JW engine.
Legacy code remains temporarily for migration tests and the independent TI demo. Native golden-frame and
session tests do not open hardware.
