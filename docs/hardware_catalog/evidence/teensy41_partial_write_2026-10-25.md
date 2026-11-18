## Partial-write fix bench record — 2026-10-25

The same authorized stationary bench checks were repeated after flashing the
partial-write fix to Teensy `15104350`, with motor power disconnected. Image
SHA-256:
`181565393a599ff3438db8e05f525ab6182650de5f83d70f6f16f58ce687e39e`.

The production configure/feedback diagnostic passed. Both 4096-retry bursts
drained intact with byte-identical IDENTIFY replies. With host reads withheld,
the runtime reported the local fault; the burst with a queued ESTOP showed the
local-fault and subsequent ESTOP transitions. Reset/recovery interlocks passed,
and final ESTOP cleanup left the board latched and unavailable.

This is a hardware regression check for the new image. The specific 16-byte TX
capacity case is covered by native tests of the actual Teensy and ESP32 bindings;
this bench run does not establish that the physical Teensy's writable capacity
was 16 bytes. GPIO stop timing and physical motor stop remain unmeasured.
