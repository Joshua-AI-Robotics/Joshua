# Supported hardware

<!-- Generated from hardware_catalog.json; do not edit by hand. -->

This is the curated model list: intended support through existing drivers
and example presets. Support scope is reviewed independently of test results.
Adding a successful test does not automatically add a model or broaden its scope.
An absent model is not necessarily incompatible with an existing driver.

See [implemented components](SUPPORTED_COMPONENTS.md) for factory implementations
and [test records](TESTED_HARDWARE.md) for dated measurements and environments.
Protobuf presets remain authoritative for runtime settings. Resolve local device
indices before running and follow the [hardware rules](../AGENTS.md).

MCP: `list_supported_hardware(kind="camera", query="XWF-1080P")`.

<a id="sunplus-spca2281-xwf-1080p"></a>

## XWF-1080P

- Hardware ID: `sunplus-spca2281-xwf-1080p` (camera).
- Manufacturer: Sunplus Innovation Technology Inc.
- Aliases: SPCA2281, Sunplus SPCA2281.
- Driver: [CvCamera / OpenCV V4L2](../robot/perception/camera/cv_camera.cc).
- Example: [sunplus_spca2281.pbtxt](../config/config_preset/example/sunplus_spca2281.pbtxt).
- Support scope: Image capture through the existing OpenCV/V4L2 driver using the example preset on Linux.
- Evidence: [separate test records](TESTED_HARDWARE.md#sunplus-spca2281-xwf-1080p).

## Maintaining the list

Edit [hardware_catalog.json](hardware_catalog.json) for stable model information.
A model can have zero or multiple tests. Add dated runs separately to
[hardware_test_records.json](hardware_test_records.json), referencing the model's
stable `hardware_id`. Do not copy model metadata into individual test records.

In a Joshua Docker development shell, regenerate both guides with:

```sh
python3 -m tools.mcp.hardware_catalog --write-docs
```

The catalog test checks schemas, duplicate IDs, unknown hardware references,
source/preset/evidence links, and generated-guide consistency. Declare new
referenced files in the test data in `tools/mcp/BUILD` for isolated Bazel checks.
