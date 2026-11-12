# Host GPIO

Digital input/output through the [board layer](../../README.md), with source
documented profiles for Raspberry Pi 5 Model B J8 and Jetson Orin Nano on
NVIDIA's P3768-0000 carrier J12. The C++ backend uses Linux GPIO character-device
ABI v2 and the existing BoardFactory cache. Creating a board opens real GPIO
lines and applies configured output values; follow the
[hardware rules](../../../AGENTS.md#hardware-safety--read-this-first).

## Contents

- [pin_resource.h](../pin/pin_resource.h) — `PinInfo` hardware metadata,
  `PinProfile` catalog identity and `PinResource` configured pin purpose/policy.
- [pin_resource.cc](../pin/pin_resource.cc) — compiled, versioned profiles,
  complete 40-position headers and pure offline resolution/validation.
- [digital_pin.h](../interfaces/digital_pin.h) — typed Boolean reads/writes.
- [host_gpio_board.cc](host_gpio_board.cc) — board ownership, rollback, stop,
  handle invalidation and shutdown.
- [linux_gpio_backend.cc](linux_gpio_backend.cc) — injectable exclusive Linux
  requests, controller/line discovery and platform checks.

## Configuration and API

The exact profile IDs are `raspberrypi/pi5` and `nvidia/orin-nano-p3768`, version
`1`. The latter excludes original Jetson Nano, Orin NX and third-party carriers.
See the complete [Pi 5](../../../config/config_preset/example/pi5_gpio.pbtxt)
and [Orin Nano](../../../config/config_preset/example/orin_nano_gpio.pbtxt)
examples. These are board API examples; no digital ROS node is launched.

```text
boards {
  name: "local_gpio"
  board_type: HOST_GPIO
  host_gpio_config {
    profile_id: "raspberrypi/pi5"
    profile_version: 1
    pins {
      name: "ready_led"
      purpose: "panel ready indicator"
      pin { signal: "J8.GPIO17" }  # Physical J8 pin 11; either form is accepted.
      direction: DIGITAL_OUTPUT
      initial_value: false
      shutdown_value: false
    }
  }
}
```

Drivers acquire the shared board using the existing factory, then use its digital
capability independently of motor channels:

```cpp
auto board = BoardFactory::GetOrCreate(board_config);
if (!board.ok()) return board.status();
auto pin = (*board)->OpenDigitalPin("ready_led");
if (!pin.ok()) return pin.status();
const auto& resource = (*pin)->resource();  // .info, .config.purpose(), identity()
return (*pin)->Write(true);
```

`Read()` and `Write()` use logical active values: with `active_low: true`, logical
true means physical low. Inputs reject writes and output-value policies. Outputs
require explicit initial and shutdown values; acquisition applies the initial
value in the kernel request, without a separate direction/write interval.
`DIGITAL_BIAS_AS_IS` leaves existing pulls alone; other bias requests must be
supported by the running kernel driver or acquisition fails. A missing pin,
unknown version, unqualified alias, duplicate claim or non-GPIO pin fails before
hardware access.

Catalog data and generated Markdown pin tables can be inspected without hardware
from a [Docker dev shell](../../../CONTRIBUTING.md#development-setup):

```bash
bazel run //robot/board/pin:describe_pins -- raspberrypi/pi5
bazel run //robot/board/pin:describe_pins -- nvidia/orin-nano-p3768
bazel run //config:joshua_validate_config -- --config config/config_preset/example/pi5_gpio.pbtxt
```

## Responsibilities

Profiles describe exposed connectors, nominal voltage domains, capabilities,
Linux controller labels/line names, alternative functions and restrictions.
Every connector position is cataloged, including power/ground and reserved
pins. An alias such as `J8.GPIO17` and its header position resolve to the same
resource. The configured purpose records robot wiring intent separately from
hardware facts. Profile versions must change when mappings are corrected.

Before requests, the backend checks exact device-tree compatible tokens and
requires a unique controller label plus line name. Linux chip numbers and global
GPIO numbers are never used as connector identities. The documented offset is
metadata; the actual offset is resolved from the running kernel. Missing or
ambiguous identity fails closed. Discovery is read-only until exclusive kernel
acquisition; ownership is rechecked there to handle races with other processes.
Only configured lines are acquired. Multiple names cannot share one line even
when both are inputs. Offline validation also catches conflicts across boards.
One consuming process owns a board; other processes should consume published
observations rather than request its lines.

On partial startup failure, already acquired pins receive shutdown values and
release in reverse order. Teardown and EmergencyStop attempt every pin, preserve
failures and invalidate retained handles. A read/write failure closes that pin
and latches its fault; a retained handle cannot retry or revive it. Restart the
owning runtime deliberately after resolving the cause. Process-wide shutdown
uses the existing BoardFactory cleanup path. Destructors provide fallback
release. Shutdown levels after release or crash need external circuitry; kernel
ownership does not establish electrical safety.

## Platform setup and evidence

The host kernel and GPIO driver must implement ABI v2 (introduced in Linux
5.10). This is independent of the container's Ubuntu version. The backend calls
the stable UAPI directly, with no libgpiod package/version dependency and no
sysfs or MMIO fallback. Both Ubuntu image builds use their Linux UAPI headers.
Expose the host's `/dev/gpiochip*` devices and readable
`/proc/device-tree/compatible` to the consuming container. Joshua's normal dev
Compose services already mount `/dev`; a deployment with restricted access must
provide these deliberately. Permission failures are reported, never bypassed.

Pi 5 requires the RP1 driver exposing `pinctrl-rp1` and matching `GPIO<n>` line
names. J8 pins 27/28 are reserved for HAT identification. Kernel-owned SPI, I2C
or UART lines must be released by the appropriate platform setup before they
can be used as GPIO.

Orin Nano requires the NVIDIA P3768 carrier and `tegra234-gpio` line names from
NVIDIA's mapping. Configure pinmux/direction/pulls using the supported Jetson
Linux pinmux/Jetson-IO procedure for the installed JetPack image. The GPIO request
does not configure pad mux or guarantee the signal reaches the header. Use
external pulls where the driver does not support bias configuration. Peripheral
pins without an upstream digital mapping remain unavailable. The catalog is
based on [NVIDIA Jetson.GPIO v2.1.12](https://github.com/NVIDIA/jetson-gpio/blob/v2.1.12/lib/python/Jetson/GPIO/gpio_pin_data.py)
and the [carrier documentation](https://developer.nvidia.com/embedded/learn/jetson-orin-nano-devkit-user-guide/hardware_spec.html);
Pi sources are recorded with its profile. Alternative functions are descriptive,
not a complete pinmux database. Current limits and actual wiring remain
unqualified; nominal 3.3V does not establish safe load capability.

Hardware-free tests cover profile resolution, cross-board claims, syscall ABI
flags, initial values, wrong platforms, ambiguous controllers, busy lines,
permission/unsupported-kernel failures, rollback and retained-handle cleanup.
Physical Pi/Jetson loopback, pull/polarity, output startup/shutdown and carrier
revision qualification remain pending. No hardware has been run for this change.

## Non-Goals

Edge events, debounce, PWM, SPI/I2C/UART drivers, STEP/DIR pulse engines and
motor feedback are unsupported. This API performs synchronous software I/O and
makes no real-time timing guarantee. Existing motor channels and JoshuaWire
configs retain their contracts. Digital ROS/action/perception adapters can use
this board capability later; GPIO must not be encoded as joint positions.
