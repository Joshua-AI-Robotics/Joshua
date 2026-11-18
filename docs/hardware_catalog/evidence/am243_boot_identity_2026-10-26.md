### Boot identity bench record — 2026-10-26 UTC

An authorized LP-AM243 run with motor power off flashed the JW TI-stack image
built with communication/target watchdogs of 20000/10000 us. Its signed appimage
SHA-256 was `47e96a9572618330621e3612e69cc2f286c954e7309ced8479695fc7da88e0ca`.
The first hardware attempt exposed a missing SA2UL firewall grant: the R5 took
a synchronous data abort while reading `0x40901000` in `SA2UL_engineEnable`.
Adding the SDK RNG example's grant to the isolated SysConfig overlay corrected
startup; both UART/TI-demo and JW artifacts cross-built afterward.

Over the direct `enp5s0` link, the corrected image advertised `am243-ec-jw`,
revision 4/layout 2, with 112-byte input/output PDOs. Two USB-C power cycles
produced boot IDs `0xc9d5fe7e` and `0xaf024889`. On each boot, five distinct
session resets and 15 correlated IDENTIFY replies preserved that boot's ID.
The probe used startup CoE SDOs only: it never requested OPERATIONAL or sent
actuator configuration, enable or target commands. This verifies RNG startup
and boot/session identity behavior on this board, not entropy quality,
EtherCAT timing, watchdog latency or physical motor safety.
