# Logitech C920 camera-to-MCP integration test

Manual test on November 8, 2026 (America/Los_Angeles), against Joshua commit
`cdba38b2cf1e98efd4008fe16657fefc2618d2cf` from [PR #150](https://github.com/Joshua-AI-Robotics/Joshua/pull/150).
This records one physical camera unit's capture and publisher restart behavior.
It is scoped integration evidence, not general model qualification. The later
MCP-only ROS subscription revision removes the manifest/launcher options used
below; this historical run does not qualify that revised implementation.

## Setup

- Logitech HD Pro Webcam C920, USB ID `046d:0892`, capture index 0.
- Ubuntu 22.04 / ROS 2 Humble / Python 3.10.12 in Joshua's CPU Docker environment.
- Real MCP Python SDK 1.30.0 over stdio; MCP protocol `2025-11-25`.
- The unchanged [camera-only preset](../../config/config_preset/example/publish_camera.pbtxt)
  selected `sensor:webcam`, MJPG, 640x480 and a requested 30 FPS, publishing
  RGB8 on `/camera_1`. No actuator or board was configured or operated.
- Preset SHA-256: `9da32a3200ebf500d3675a01cf710efb32573a80fa0f2942739486537a896ee9`.
- ROS domain 93 with `ROS_LOCALHOST_ONLY=1`. Launcher and MCP shared a private
  observation directory under the same container UID.

## Procedure

1. Built the launcher, camera publisher, image observer, validator, inspector
   and MCP server with the Humble task's u22, x86-base and Python 3.10 settings.
2. Initialized a real SDK stdio session with the configured image observer.
   Verified all nine default tools plus `get_camera_image` were available.
3. Called `validate_config` for `example/publish_camera.pbtxt` and received a
   valid integrity report. With Joshua stopped, the image tool returned
   unavailable and no image.
4. Started Joshua with that preset, `--observe_camera=webcam` and the private
   observation directory. Requested `get_camera_image` for `sensor:webcam`.
5. Decoded the returned standard MCP JPEG block and checked actual dimensions,
   MIME type, source metadata and the serialized response bound. Requested
   another image after 1.5 seconds and compared sample identity and JPEG data.
6. Stopped Joshua gracefully while keeping the SDK/MCP session open. Verified
   that image delivery became unavailable and carried no image block.
7. Restarted Joshua using the same preset and observation directory. Without
   restarting the MCP session, received and decoded a fresh image associated
   with a new runtime instance and image-reader sample identity.
8. Closed MCP and stopped Joshua. Verified successful launcher exits, released
   camera devices, removed test container and a clean candidate checkout.

## Results

| Capture | Decoded dimensions | JPEG bytes | Identity/content observation |
| --- | --- | --- | --- |
| First | 640x480 | 28,628 | Fresh sample from the configured publisher |
| Second | 640x480 | 28,402 | Different sample ID and JPEG bytes |
| After Joshua restart | 640x480 | 27,797 | New runtime instance and image-reader sample identity |

**PASS:** physical C920 capture traversed the existing C++ OpenCV driver, ROS 2,
production passive C++ image reader, Python adapter and actual MCP stdio SDK.
Three JPEG results decoded successfully. A returned image was also visually
inspected in Codex. Publisher stop produced unavailable-without-image, and
publisher restart restored fresh image delivery within the same MCP session.
Both launcher shutdowns exited with code 0; the camera was released afterward.

Images and raw responses are retained locally because the camera frames contain
private room imagery. This record retains the procedure, measured byte counts
and observed result without publishing those frames or host-specific identities.

## Remaining acceptance and limits

- A separately configured AI client consuming the images and describing a
  deliberate scene change across two requests was not tested.
- Physical USB unplug/replug was not exercised. The remaining check keeps MCP
  running, removes the C920, verifies that stale/unavailable results contain no
  image, then reconnects and verifies fresh image delivery without reinstalling.
- Requested/negotiated FPS is not measured delivery performance. This short run
  does not qualify sustained frame rate, higher resolutions, Jazzy, another
  camera unit, exposure timing or camera calibration.
- Freshness describes comparable host receipt time. Camera acquisition
  freshness remains unknown; frames establish no robot movement or stop result.
- Prior [#129](https://github.com/Joshua-AI-Robotics/Joshua/pull/129) MCP disconnect
  and [#130](https://github.com/Joshua-AI-Robotics/Joshua/pull/130) synthetic
  observation recovery tests remain useful coverage. They do not replace the
  current physical C920/image-adapter USB check.

For another run, use the [camera/MCP guide](../../tools/mcp/README.md#optional-configured-camera-image)
and [hardware rules](../../AGENTS.md#hardware-safety--read-this-first), inspect the
entire preset and confirm the camera's current capture index and operator
readiness. Retain the code/config identity and actual SDK image results. A
historical test does not authorize another hardware run.
