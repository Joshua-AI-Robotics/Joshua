# Supported components

This is a guide to components implemented on `develop`, with links to the
code that selects them and to representative checked-in presets. It is not a
second configuration registry: the protobuf schemas, model manifests, and
factories remain authoritative. Presets show how to configure a path; their
presence does not prove it has been run. A listed implementation does not mean
every combination has been tested on hardware. Check the linked preset and the
[hardware rules](../AGENTS.md#hardware-safety--read-this-first) before any run.

For physical models, aliases, drivers, presets, and intended support scope,
use the separate [supported hardware list](SUPPORTED_HARDWARE.md), generated
from the [model catalog](hardware_catalog.json). Dated environments,
measurements, and limitations live in [test records](TESTED_HARDWARE.md),
referencing models by stable hardware ID. A test does not automatically add a
model or broaden its support scope; an absent model is not necessarily
incompatible with an existing driver.

For LLM discovery, [MCP](../tools/mcp/README.md) exposes
`list_supported_components()` for factory implementations,
`list_supported_hardware(kind="camera", query="XWF-1080P")` for model discovery,
and `list_hardware_test_records(hardware_id="sunplus-spca2281-xwf-1080p")` for
the model's historical runs. These queries do not establish readiness of a
connected device.

## Hardware path

The [board factory](../robot/board/factory/board_factory.cc) constructs the
following board types. Boards own channels; [motor/channel validation](../robot/board/factory/motor_channel_validation.cc)
determines which actuator can use each channel.

| Board | Implemented path | Example or evidence |
| --- | --- | --- |
| `FEETECH_BUS` | [Serial servo bus](../robot/board/feetech_bus/feetech_bus_board.cc) | [SO-100 teleoperation preset](../config/config_preset/so100/teleoperate.pbtxt) |
| `AM243` | [JoshuaWire over serial or paired message/cyclic EtherCAT](../robot/board/joshua_wire/joshua_wire_board.cc) | [Serial](../config/config_preset/example/am243_serial_demo.pbtxt), [JW EtherCAT](../config/config_preset/example/am243_jw_ethercat_demo.pbtxt); current AM243 channels are software-only, with no STEP/DIR GPIO output |
| `TEENSY41` | [Serial JoshuaWire](../robot/board/teensy/teensy_board.h) | [Stepper preset](../config/config_preset/example/teensy_stepper_demo.pbtxt) |
| `ESP32` | [Serial JoshuaWire](../robot/board/esp32/esp32_board.h) | [Stepper preset](../config/config_preset/example/esp32_stepper_demo.pbtxt) |
| `HOST_GPIO` | [Linux digital pins](../robot/board/host_gpio/README.md), Pi 5 J8 / Orin Nano P3768 J12 | [Pi 5](../config/config_preset/example/pi5_gpio.pbtxt), [Orin Nano](../config/config_preset/example/orin_nano_gpio.pbtxt); board API only, physical qualification pending |
| `MOCK` | [Test-only board](../robot/board/mock/mock_board.cc) | [Board factory tests](../robot/board/factory/board_factory_test.cc) |

`ARDUINO_UNO` appears in the
[board schema](../robot/board/proto/board.proto), but the factory rejects it
as unimplemented. The [board-layer example](../config/config_preset/example/board_layer_example.pbtxt)
contains an Arduino/UDP
example and is not evidence that either path works. For physical bring-up and
firmware verification, use the [firmware status table](../firmware/README.md#current-firmware-records);
in particular, ESP32 protocol verification does not establish motor rotation.

The [communication factory](../robot/comm/factory/comm_factory.cc) provides
serial byte-stream and message transports, plus paired EtherCAT message/cyclic
transport. EtherCAT requires explicit JoshuaWire protocol, endpoint, and timing
configuration. The standalone `CYCLIC` transport and TI-demo runtime path are retired.
Ethernet UDP is declared but rejected as unimplemented. The
[communication guide](../robot/comm/README.md) describes these combinations.

The [action factory](../robot/action/factory/action_factory.h) selects STS3215
servo and NEMA17 stepper drivers. It rejects the retired `MOTOR_TI_DEMO` type.
The [perception factory](../robot/perception/factory/perception_factory.cc)
selects OpenCV camera (`IMAGE`), LDS01 lidar (`RANGE_SCAN`), STS3215
position sensor (`POSITION`), and ALSA audio (`AUDIO`). IMU and force/torque
implementations are unavailable. Examples are the
[camera](../config/config_preset/example/publish_camera.pbtxt),
[lidar](../config/config_preset/example/lds01_lidar.pbtxt), and
[position publisher](../config/config_preset/so100/encoder_publish.pbtxt) presets.
These entries describe software selection, not a tested-model list. In
particular, the OpenCV implementation serves compatible camera models through
configuration; Sunplus uses that existing implementation.

## AI, simulation, and ROS 2 data

| Area | Implemented scope | Source and examples |
| --- | --- | --- |
| Model adapters | Random noise and SmolVLA have registered adapters and manifests. | [Model build registry](../ai/models/BUILD), [random-noise preset](../config/config_preset/so100/random_noise.pbtxt); SmolVLA has [a manifest](../ai/models/smolvla/model.textproto) but no checked-in preset. |
| Simulation | MuJoCo interactive, passive, mirror, and offscreen modes; Isaac Sim backend. | [Simulation guide](../simulation/README.md), [SO-100 interactive preset](../config/config_preset/so100/sim_interactive.pbtxt). No Isaac Sim preset ships in the repo. Mirror mode's [preset](../config/config_preset/so100/sim_mirror.pbtxt) also opens a real serial bus. |
| ROS 2 types | The enum and Python resolver map message classes for generic subscription/data collection. A mapping entry alone does not establish support in every node. | [Type enum](../ros2/proto/ros2_data_type.proto), [resolver](../ros2/ros2_type_resolver.py). |
| Concrete publishers | Camera publishes `Image`; lidar publishes `PointCloud2`. Position publishers select compiled scalar, array, or JointState conversions from config. Message support depends on the node. | [Camera](../ros2/camera_publisher.cc), [lidar](../ros2/lidar_publisher.cc), [position](../ros2/position_publisher.cc), [typed position contracts](../ros2/README.md#typed-position-and-actuator-messages). |

The [preset validation test](../config/config_preset/config_preset_validation_test.cc)
loads every checked-in preset and runs `config::ValidateConfig` without opening
devices. That check does not construct boards or verify that every declared
board and transport is implemented. Factory and driver tests exercise selected
behavior with test doubles. These software checks do not prove that a preset's
physical wiring, firmware, or motion path is ready. See
[configuration guidance](../config/README.md) and
[simulation prerequisites](../simulation/README.md) before using an example.
