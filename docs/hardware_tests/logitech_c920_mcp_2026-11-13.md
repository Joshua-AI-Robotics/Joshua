# Logitech C920: native ROS 2 MCP camera check

**Result: PASS, 2026-11-13.** An operator-confirmed physical C920 produced
images through the normal Joshua camera runtime and the production MCP server.
This supersedes the older implementation's camera-path evidence in the
[2026-11-08 record](logitech_c920_mcp_2026-11-08.md).

## Tested candidate and setup

- PR #150 revision `5eae3ba4edb70086ef38e9c8d11b420db43a1789`, including
  the shared-node/distinct-topic resolver fix, before the later cleanup and
  standalone-launch updates. Tested `ros_source_binding.cc`
  SHA-256: `0e6e6ab5905a5e65a45e28dcee1bd8897f3dc531772f8c7722661b5d4b38e3f6`.
- Ubuntu x86-64 host, existing Ubuntu 22.04 / ROS 2 Humble Docker image,
  CPU-only. No host dependency installation.
- Unmodified `example/publish_camera.pbtxt`: camera-only `MODE_TEST`,
  `/dev/video0`, 640×480, MJPG, 30 fps. The driver reported 640×480 at 30 fps.
- Runtime: normal `joshua_main --config <preset>`, unprivileged UID 1000,
  video-group access and only `/dev/video0` exposed.
- MCP: separate read-only, unprivileged container, no GPU or device access.
  Explicit runtime network/IPC namespace sharing; no host networking.
- Consumer: actual MCP Python SDK over stdio. No synthetic ROS publisher,
  camera-specific launcher flags or producer manifest.

## Results

| Check | Result |
|---|---|
| Preset validation through MCP | Valid |
| Camera runtime absent | Unavailable, no image |
| Three live captures | Fresh JPEGs, distinct sample IDs, decoded 640×480 pixels |
| Runtime stopped | Unavailable, no image |
| Runtime restarted, same MCP session | Fresh JPEG, new sample ID |
| Final shutdown | MCP exited cleanly; test containers removed; camera released |

All four JPEGs decoded with OpenCV and stayed within the 512 KiB image and
1 MiB result limits. JPEG sizes were 24,544–28,402 bytes. Successful calls
took 5.87–10.77 ms; discovery to the first fresh sample took about 1.44 s,
and runtime-restart recovery about 1.23 s. These are four local observations,
not a load or latency benchmark. Initial covered-lens images were dark;
after the operator opened the cover, decoded images showed the lit scene.
Images and raw logs remain local and are not published.

Runtime identity and loaded-config association remained unknown, as specified.
This check does not establish USB unplug/replug recovery, multi-camera hardware
support, another ROS stack's physical behavior, AI IDE image rendering, or
fresh-host installation. Canonical software tests separately passed 71/71
targets on both Humble and Jazzy; the distinct-topic/shared-node topology has
resolver regression coverage rather than a two-camera physical test.
