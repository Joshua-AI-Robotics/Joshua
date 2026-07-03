# EtherCAT Transport

`robot/comm/ethercat/` is the home for generic EtherCAT master-side transport
code. It should not contain AM243 actuator semantics; those belong in action
drivers that consume a transport abstraction.

## Responsibilities

- Discover and configure EtherCAT slaves.
- Transition slaves through INIT, PRE-OP, SAFE-OP, and OPERATIONAL.
- Own cyclic PDO exchange and working-count validation.
- Expose process-data access to higher-level drivers without leaking a specific
  master implementation into the action layer.

## AM243 Constraint

The current LP-AM243 TI EtherCAT simple demo works with split LRD/LWR process
data cycles and does not work with LRW cycles. A Joshua transport backend used
with this firmware must force split LRD/LWR. LRW should only be revisited after
the board firmware or EEPROM/ESI configuration changes.

## Non-Goals

- AM243-specific actuator mapping.
- UART or serial flashing/debug tools.
- SOEM-specific types in public action-driver headers.
