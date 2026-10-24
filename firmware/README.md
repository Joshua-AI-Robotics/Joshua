# Joshua Firmware

Joshua-owned MCU firmware and host bindings use JoshuaWire **0.2.0 / wire
revision 4** from the same checkout. Earlier JoshuaWire implementations are
removed. Existing devices need an intentional firmware update before use with
this host; neither runtime initialization nor tests flash hardware.

## Firmware records

| Board | Current artifact | Guide |
| --- | --- | --- |
| Teensy 4.1 | STEP/DIR over native USB, revision 4 | [teensy/41](teensy/41/README.md) |
| ESP32 | STEP/DIR over UART, revision 4 | [esp32](esp32/README.md) |
| AM243 | Revision-4 software actuator over UART alongside TI's independent vendor EtherCAT demo | [AM243 overlay](am243/joshua_dual_transport/README.md) |
| AM243 vendor demo | TI EtherCAT sample; vendor sources stay in the external SDK | [TI demo](am243/ti_ethercat_simple_demo_v1/README.md) |
| Arduino | Not implemented | [Arduino](arduino/README.md) |

`common/` provides one set of C codecs, correlated endpoint/session, profile
schemas and device runtime for Bazel and MCU builds. Teensy/ESP32 additionally
share the Arduino GPIO backend and revision-4 application. Native PTY integration
tests compile those production sources with simulated GPIO and clock functions.

## Flashing

Each board guide follows [FLASHING_TEMPLATE.md](FLASHING_TEMPLATE.md). Build,
review and intentionally upload the correct board artifact. Cross-builds and
native integration tests do not establish physical motion/safety verification;
verify the newly flashed image against the actual wiring separately.
