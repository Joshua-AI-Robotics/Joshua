# JW2 binding for SOES

This is the board-independent slave-stack adapter. The host still uses the
same JoshuaWire EtherCAT transport, with SOEM underneath. Changing the slave
stack or board does not change the host wire contract.

- `joshua_ethercat_soes.{h,c}` registers the fixed CoE dictionary and maps the
  80-byte PDO images to the [shared profile](../joshua_ethercat_profile.h).
  SOES parses CoE and owns the EtherCAT state machine; Joshua owns sessions,
  command correlation, drive callbacks and watchdog policy.
- `ecat_options.h` defines the mailbox/SM/PDO sizes and addresses. Each board's
  SII must match. FoE, EoE, dynamic mapping and Complete Access are not supported.
- `cc.h` supplies GCC/Clang packing, byte-order and atomic primitives for native
  tests and TI ARM Clang. Only little-endian targets are currently supported.

The adapter owns one SOES instance (the upstream stack uses globals). A board
initializes its ESC, confirms DL-ready with a deadline, initializes a shared
profile, and calls `JoshuaSoesInit`. It supplies monotonic time, short lock/unlock
callbacks and optionally an emulated EEPROM handler. One task calls
`JoshuaSoesPoll`; a separate task **must** tick the profile using the same lock.
Stack/HAL I/O is never performed inside that lock. The profile uses elapsed time,
not SOES's poll-count watchdog. This is not protection against a halted CPU.

Management objects use subindex zero and exact sizes. Every object fits the
512-byte mailbox, so writes must arrive in one transfer; segmented writes are
rejected before copying or executing commands. PDO image objects cannot be
read/written through CoE. SII and CoE `0x1018` identity come from the board; the
adapter does not hard-code an AM243 or TI identity.

The [pinned SOES dependency](../../../third_party/soes/README.md) is fetched at
build time, not vendored into Joshua. PlatformIO excludes this directory unless
a board explicitly integrates SOES. Its license is not Joshua's Apache license.

The existing `joshua_wire_v2_session_test` suite exercises the real SOES CoE
parser/dictionary and PDO pack/unpack functions using memory-backed ESC access.
It covers descriptor/identity/reset, access and length rejection, command
execution, duplicate retention, target expiry and OP loss. It does **not**
validate an ESC hardware driver or timing on a real EtherCAT bus.

The initial [AM243 port](../../am243/joshua_dual_transport_v1/README.md#opt-in-soes-replacement)
builds, but hardware qualification and evaluation-stack retirement are pending.
