# Xbox controller input

Standalone Linux Xbox controller reader and ROS 2 publisher. This package lives
under `tools/teleop/` because it provides operator input; the repository's
top-level `utils/` contains shared Joshua software and system utilities.

The publisher reads evdev events through libevdev and publishes controller state
on ROS 2. It has no robot-specific mappings or direct motor-control path. The
launcher does not start it through a preset.

## Build

Run these commands from the repository root. The supported Docker images provide
ROS 2 and libevdev; compilation needs neither a GPU nor a connected controller.

Ubuntu 22.04 / ROS 2 Humble:

```bash
docker compose run --rm test-u22 bazel build --config=u22 --config=x86-base \
  --@rules_python//python/config_settings:python_version=3.10 \
  //tools/teleop/xbox_controller:xbox_controller_publisher
```

Ubuntu 24.04 / ROS 2 Jazzy:

```bash
docker compose run --rm test-u24 bazel build --config=u24 --config=x86-base \
  --@rules_python//python/config_settings:python_version=3.12 \
  //tools/teleop/xbox_controller:xbox_controller_publisher
```

## Run

Connect the controller before starting the publisher. Open a supported
[Docker developer shell](../../../docs/GETTING_STARTED.md) with read access to
`/dev/input/event*`. Inside the shell:

```bash
bazel run //tools/teleop/xbox_controller:xbox_controller_publisher
```

The first positional argument can be a protobuf preset. The executable validates
it and uses its process clock and logging settings; it does not launch that
preset's robot graph. Without an argument it uses protobuf defaults.

```bash
bazel run //tools/teleop/xbox_controller:xbox_controller_publisher -- path/to/preset.pbtxt
```

Stop the publisher with Ctrl+C. From another ROS-enabled shell, inspect its output:

```bash
ros2 topic echo /xbox_controller_data
```

Running the publisher opens physical input devices. Any robot receiving the
published data needs its own command mapping and operating procedure. Follow
the repository's [hardware rules](../../../AGENTS.md#hardware-safety--read-this-first).

## ROS interface

The node is named `xbox_controller_publisher`. It publishes
`std_msgs/msg/Float32MultiArray` on the relative topic `xbox_controller_data`
with depth 10. The topic resolves to `/xbox_controller_data` in the default
namespace. The array layout is empty and `data` contains 19 values:

| Indices | Values in order | Scaling |
| --- | --- | --- |
| 0–3 | Left stick X, left stick Y, right stick X, right stick Y | Raw value / 32768 |
| 4–5 | D-pad X, D-pad Y | Raw value |
| 6–7 | Left trigger, right trigger | Raw value / 1023 |
| 8–18 | A, B, X, Y, left bumper, right bumper, start, back, left stick click, right stick click, guide | Raw button state |

The timer runs every 0.016 RobotTime seconds, approximately 62.5 Hz. A paused
RobotTime pauses publication; this is not a real-time delivery guarantee.

## Compatibility and limits

The executable replaces `//ros2/utils:xbox_controller_publisher`. Its topic,
payload order, normalization, clock behavior and node name are preserved. The
input library moved from `utils/xbox_controller/` and is private to this package.
The legacy direct STS3215 control scripts remain removed.

Discovery selects the first accessible event device whose name matches one of
the existing Xbox/Microsoft name variants. It has no explicit device selector
or multi-controller support. Axis ranges are assumed rather than discovered.
State starts at zero and is updated from input events. There is no automatic
reconnection or input-health field; publication does not establish fresh input
or a physical stop guarantee.

## Development

See [AGENTS.md](AGENTS.md) for resource ownership, implementation rules and
verification requirements.
