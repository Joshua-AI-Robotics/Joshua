# Host GPIO and peripherals

Digital input/output through the [board layer](../../README.md), with source
documented profiles for Raspberry Pi 5 Model B J8 and Jetson Orin Nano on
NVIDIA's P3768-0000 carrier J12. The C++ backend uses Linux GPIO character-device
ABI v2 and the existing BoardFactory cache. Creating a board opens real GPIO
lines and applies configured output values; follow the
[hardware rules](../../../AGENTS.md#hardware-safety--read-this-first).

## Contents

- [pin_source.h](../pin/pin_source.h) — reusable hardware definitions and typed
  `PinFunction` queries, independent of robot configuration and hardware access.
- Board tables: [Pi 5 J8](../pin/profiles/raspberry_pi5.cc) and
  [Orin Nano J12](../pin/profiles/jetson_orin_nano.cc), one entry per physical pin.
- [pin_resource.h](../pin/pin_resource.h) — a configured claim on a source pin,
  including its robot-specific purpose and digital I/O policy.
- [pin_resource.cc](../pin/pin_resource.cc) — offline wiring resolution and validation.
- [digital_pin.h](../interfaces/digital_pin.h) — typed Boolean reads/writes.
- [host_gpio_board.cc](host_gpio_board.cc) — board ownership, rollback, stop,
  handle invalidation and shutdown.
- [linux_gpio_backend.cc](linux_gpio_backend.cc) — injectable exclusive Linux
  requests, controller/line discovery and platform checks.

## Pin definitions for drivers

The board tables are the source of physical pin definitions. For example, both
headers have physical pin 1 = 3.3V, pin 2 = 5V and pin 6 = ground. On Pi 5,
pin 12 = GPIO18 with digital I/O and RP1 PWM0 channel 2 as alternative functions;
on Orin Nano P3768, pin 15 = GPIO12 with PWM controller `3280000.pwm`, channel 0.
A pin can have several functions selected by pinmux.

Drivers can inspect these facts before any robot wiring configuration or device
acquisition. Include `pin_source.h` and depend on `//robot/board/pin:pin_source`:

```cpp
auto source = FindPinProfile("raspberrypi/pi5", 1);
if (!source.ok()) return source.status();
auto supply = (*source)->FindPin(2);  // kind == kPower, voltage_mv == 5000
auto pwm = (*source)->RequirePin(12, PinFunction::kPwm);
if (!pwm.ok()) return pwm.status();
const auto& mapping = *(*pwm)->pwm;  // controller == "rp1_pwm0", channel == 2
```

`FindPin` looks up any physical position, including power and ground.
`RequirePin` validates a driver's required function and rejects reserved pins.
Functions are typed (`kDigitalInput`, `kDigitalOutput`, `kPwm`, `kI2c`, `kSpi`,
`kUart`, `kI2s`, `kClock`); drivers do not parse descriptive text. GPIO mappings
and documented PWM controller/channel identities come from the same source.
Pi 5 pins 8/12 share PWM0 channel 2, and pins 10/35 share channel 3: the PWM
backend enforces controller/channel ownership as well as physical-pin
ownership. Hardware capability metadata does not install a PWM or peripheral
backend, configure pinmux, or establish current availability. Other peripheral
routes and functions outside the catalog's categories remain undescribed.

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

Edge events, debounce, STEP/DIR pulse engines and motor feedback are unsupported. This API performs synchronous software I/O and
makes no real-time timing guarantee. Existing motor channels and JoshuaWire
configs retain their contracts. Digital ROS/action/perception adapters can use
this board capability later; GPIO must not be encoded as joint positions.


## PWM, SPI, I2C and UART

`Board.peripherals` declares named endpoints with a required `purpose`. Drivers
use `OpenPwm`, `OpenSpi`, `OpenI2c` or `OpenUart` on the same cached board.
PWM exposes normalized duty in [0,1] and explicit Enable/Disable; startup always
leaves it disabled. SPI exposes 8-bit full-duplex `Transfer(bytes)` (1..32 bytes).
I2C exposes `Transfer(write_bytes, read_count)` (combined total 1..32), with a
repeated start when both are present. UART exposes raw 8N1 `ByteStream` operations
of 1..32 bytes; separate writes/reads are not atomic transactions. These are bus
operations; sensor and actuator command interpretation belongs in drivers.

```text
peripherals {
  name: "cooling_fan"
  purpose: "PWM speed command"
  pwm {
    pin { header_pin: 32 }
    frequency_hz: 1000
    initial_duty_cycle: 0
  }
}
```

SPI settings require `device`, exact device-tree controller-node basename,
`chip_select_index` (explicit 0 or 1), `speed_hz`, and mode 0..3. Pin roles are
`clock`, `mosi`, `miso`, `chip_select`. Pi supports physical groups 23/19/21/24
and 23/19/21/26. Orin also supports 13/37/22/18 and 13/37/22/16. Controller and
CS identity are checked against the opened character device, independent of Linux
bus numbering. Enable spidev and the correct pinmux before use.

I2C settings require `device`, controller-node basename, 7-bit address 8..119,
and `timeout_ms` 1..100. Host `frequency_hz` must be omitted: the kernel/device
tree sets clock speed. Pi supports SDA/SCL pins 3/5; Orin supports 3/5 and 27/28.
The kernel adapter must support raw I2C. Compatible endpoints share an adapter
and serialize combined transactions. Kernel-bound target addresses are rejected;
no FORCE ioctl is used. A short/error transfer faults the shared adapter.

UART selects TX/RX 8/10, `controller`, and raw serial `settings { port: ...
baudrate: ... }`; the existing serial owner supplies I/O deadlines and exclusive
tty ownership. Neither bus numbers nor serial port names identify header routes.
The controller assertion and documented pin group must both match. Pinmux is
configured by the operator; the backend never rewrites a device tree or pad mux.

PWM discovers controllers by device-tree identity, then uses the Linux PWM sysfs
ABI. It refuses existing exports and disables/unexports on close. Pi uses RP1
PWM0; Orin uses the cataloged Tegra controllers. Export/mux availability depends
on installed kernel/overlays. The backend does not promise a physical waveform
until hardware qualification. PWM setup write failures attempt disable and
unexport and preserve cleanup errors.

The pure resolver rejects pin/mux and shared PWM-channel conflicts before I/O,
including aliases and digital pins. Production boards take cooperative per-user
pin/mux locks under `/run/lock/joshua/uid-<uid>` (override `lease_directory` with an
absolute shared directory). Compose mounts that directory across containers.
Persistent lock files prevent unlink/lock races. These reservations coordinate
Joshua processes of the same UID; kernel requests, PWM export ownership, device
locks and permissions remain necessary for other processes. Shared SPI signals
and compatible I2C buses are supported within one board; separate boards/processes
cannot split the same pins. Give each physical bus one board owner.

Teardown disables digital outputs and PWM before releasing bus endpoints and
invalidates every retained handle. SPI/I2C errors are returned without automatic
retry; a failed write may already have reached the target. Kernel SPI/I2C ioctls
have driver-dependent completion time; configured I2C timeout is quantized by
Linux and is not a hard wall-clock deadline. EmergencyStop cannot preempt an
in-flight kernel ioctl. Hardware stop timing, loopback and electrical behavior
remain unqualified. Tests use fake syscalls/sysfs and real temporary file locks;
no physical peripherals are exercised.
