## Historical delayed-dispatch bench record — 2026-10-25

This record covers JW05 before the factory migration retired its TI-demo path.

An authorized AM243 bench run exercised the production `EthercatMaster` and
SOEM backend with the pre-dispatch deadline fix. The direct `enp5s0` link
discovered one `TI EtherCAT Toolkit for AM243X.R5F` slave: manufacturer
`0xe000059d`, product `0x54490025`, revision `0x00010000`, with 8-byte input and
output PDOs. The existing TI demo image was used; no firmware was flashed.

The Docker Ubuntu 22 probe used a 50 ms cycle period, 5 ms process timeout,
10 ms state-check budget and 10 s startup-operation timeout. Eleven normal
exchanges returned WKC `3/3`; the demo echoed seed `0x5a` after the initial
pipeline delay. The probe then held the owner mutex after a completed cycle
for 150 ms to delay dispatch beyond the next cycle's completion deadline.

On resumption, the worker reported `DEADLINE_EXCEEDED` before dispatch. The
exchange count advanced from 11 to 12, and that sole additional exchange
carried the all-zero demo stop image with WKC `3/3`. No normal output image
was transmitted for the expired cycle. StopCyclic and teardown returned OK.

This verifies the delayed-dispatch path against a real TI-demo slave under an
injected host delay. It does not establish production scheduling bounds,
physical motor stop timing or qualification of other NICs/slave images.
