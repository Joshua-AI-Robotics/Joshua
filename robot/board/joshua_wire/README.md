# Shared JoshuaWire engine

`JoshuaWireBoard` supplies identity checks, channel configuration, correlated
session reset, command dispatch and safe teardown. `JoshuaWireSession` owns
message IDs and response matching; communication adapters own device I/O and
timeouts. Native integration tests run the production command handler and
firmware endpoint, with no physical device.

During this intermediate migration, existing MCU consumers still use
`LegacyJoshuaWireBoard`. The next stage switches their firmware and board
bindings together. The correlated engine can already be instantiated directly
with message or paired capabilities. `FramedSerialTransport` assembles complete
frames over the serial mechanism and retains fixed-length forwarding for vendor
consumers.
