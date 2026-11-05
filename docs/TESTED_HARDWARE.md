# Hardware test records

<!-- Generated from hardware_test_records.json; do not edit by hand. -->

These are historical tests of specific units and software setups. The
[supported-hardware list](SUPPORTED_HARDWARE.md) defines intended model support
separately. Measurements below do not establish current readiness, every hardware
revision's compatibility, or permission to run a device.

MCP: `list_hardware_test_records(hardware_id="sunplus-spca2281-xwf-1080p")`.

<a id="sunplus-spca2281-xwf-1080p"></a>

## XWF-1080P

[Model details and support scope](SUPPORTED_HARDWARE.md#sunplus-spca2281-xwf-1080p).

### sunplus-spca2281-2026-11-04-humble-640x480

- Hardware ID: `sunplus-spca2281-xwf-1080p`.
- Tested: 2026-11-04 at Joshua `74ecbcc618658358de2bbc9048fe9d60df7acadb`.
- Environment: os=Ubuntu 22.04 (Docker), ros_distribution=Humble, architecture=x86_64.
- Evidence: [manual test record](../docs/hardware_tests/sunplus_spca2281_2026-11-04.md).

Recorded results:

```json
{
  "capture_format": "MJPG",
  "width": 640,
  "height": 480,
  "encoding": "rgb8",
  "received_frames": 91,
  "duration_seconds": 10.634,
  "measured_fps": 8.46,
  "bytes_per_frame": 921600
}
```

Limitations:

- Short capture and desktop preview on one physical unit; no endurance or cross-platform qualification.
- Requested 30 FPS; measured 8.46 FPS with a reliable ROS subscriber. Direct OpenCV capture also achieved approximately 8 FPS.
- 1080p capture was not tested. The model name and advertised modes are not verification evidence.
- The preset uses camera index 0; resolve the connected device before running because indices can change.
- USB VID:PID and model aliases identify a candidate model, not a unique physical unit or a guarantee for every hardware revision.
