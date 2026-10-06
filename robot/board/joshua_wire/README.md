# Shared JoshuaWire engine

`JoshuaWireBoard` supplies identity checks, channel configuration, correlated
session reset, command dispatch and safe teardown. `JoshuaWireSession` owns
message IDs and response matching; communication adapters own device I/O and
timeouts. Native integration tests run the production command handler and
firmware endpoint, with no physical device.

MCU serial consumers now use the correlated engine and migrated firmware.
`LegacyJoshuaWireBoard` remains temporarily for migration regression tests. The correlated engine can already be instantiated directly
with message or paired capabilities. `FramedSerialTransport` assembles complete
frames over the serial mechanism and retains fixed-length forwarding for vendor
consumers.
