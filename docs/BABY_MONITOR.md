# Experimental Pi audio/video bring-up

This branch adds independent YAMNet and video inference processes on top of the
reusable ALSA audio pipeline. Both publish versioned JSON events in ROS
`std_msgs/String`. A separate utility streams video and audio to a browser over
Tailscale. Notification delivery and rolling media recording are later work. `av_check` is a bounded diagnostic recording, not the
planned two-minute ring buffer.

The initial video model is EfficientDet Lite0 (COCO object labels) with a simple
whole-frame motion fraction. It is not trained to assess infant safety. YAMNet
reports cry scores with configurable consecutive-window persistence; scores are
not calibrated probabilities. Room-specific detection accuracy is unmeasured.

## Build and compatibility

Target: Ubuntu 24.04 ARM64, ROS 2 Jazzy, Python 3.12. Use Docker for builds:

```bash
TARGET=//launcher:joshua_pkg docker compose run --rm build-u24-arm64
```

With an existing Ubuntu 24.04 ARM64/Jazzy development environment, Bazel can
also run directly from the repository:

```bash
source /opt/ros/jazzy/setup.bash
bazel build --config=u24 --config=arm64-base \
  --@rules_python//python/config_settings:python_version=3.12 //launcher:joshua_pkg
```

The direct command writes `bazel-bin/launcher/joshua_pkg.tar.gz`. The Compose
task additionally copies a versioned archive into `dist/u24/arm64/`.
The architecture flag alone does not provide a cross-compilation sysroot;
the build environment must supply the matching ARM64 ROS and system libraries.

Models are downloaded at build time from Google's Raspberry Pi examples and
verified by SHA-256 in `MODULE.bazel`; the package includes their model files,
embedded labels, pinned LiteRT runtime, and Python runfiles. It does not download
models or install Python packages on the Pi at launch. Google model sources:
[YAMNet](https://github.com/tensorflow/examples/tree/master/lite/examples/audio_classification/raspberry_pi)
and [EfficientDet](https://github.com/tensorflow/examples/tree/master/lite/examples/object_detection/raspberry_pi).
The focused `joshua_pkg` target includes the monitoring node graph, avoiding
the general package's unrelated SmolVLA CUDA dependencies.
Preserve the entire extracted archive; do not copy just its executable.

The archive is not a standalone OS image. The Pi needs matching ROS 2 Jazzy,
OpenCV shared libraries, and ALSA runtime (`libasound2t64` on Ubuntu 24.04).
It can run inside a matching Joshua container with `/dev/video*` and `/dev/snd`
exposed. Diagnostics use `alsa-utils` and `v4l-utils`. No GPU is needed.

Install the ALSA runtime on the Ubuntu 24.04 Pi before starting capture:

```bash
sudo apt-get update
sudo apt-get install -y libasound2t64
```

Without it, both capture publishers can exit with
`libasound.so.2: cannot open shared object file` because they share the perception
factory's audio dependency.

## Copy and inspect

From the build machine, substitute the actual generated filename:

```bash
scp dist/u24/arm64/joshua_pkg-*-u24-arm64.tar.gz "<SSH_USER>@<PI_LAN_IP>:~/"
```

On the Pi, extract into a new directory and locate the bundled preset:

```bash
mkdir -p ~/joshua-av-test
cd ~/joshua-av-test
tar -xzf ~/joshua_pkg-*-u24-arm64.tar.gz
source /opt/ros/jazzy/setup.bash
cd joshua_main
find . -path '*/baby_monitor/pi5_usb.pbtxt'
```

Copy the preset to `./pi5_usb.pbtxt` for local edits. Select the camera index and
ALSA device explicitly. `arecord -l` lists hardware devices; `arecord -L` lists
ALSA names. This preset uses `plughw:CARD=Camera,DEV=0` for the PC-LM1E Camera
microphone. For a different microphone, replace `Camera` with its ALSA card name.
Using the card name avoids a shifting numeric card index. The Pi's `default`
device may select an HDMI playback device with no capture input.
The ALSA plug device converts the microphone's native channels and rate to mono
16 kHz, the required YAMNet input. `v4l2-ctl --list-devices` and
`v4l2-ctl --device=/dev/video0 --list-formats-ext` show camera device capabilities.
Use a supported format/resolution/FPS. The preset starts at MJPG 640x480/15 FPS.

The camera driver logs the negotiated resolution and FPS on opening. Capture
settings are applied on lazy opening and reopening; some cameras negotiate a
different rate. No claim of exact synchronization is made: image timestamps are
ROS publish times and audio timestamps estimate the oldest unread ALSA sample.

Before accessing hardware, the package can test its bundled models and codecs:

```bash
./ros2_node_wrapper.sh monitor_selftest
```

## Run capture and inference

After checking the local preset and connecting the webcam/microphone:

```bash
./joshua_monitor_main --config "$PWD/pi5_usb.pbtxt"
```

Six processes should start: `camera_publisher`, `audio_publisher`, `yamnet_node`,
`video_inference_node`, `media_stream_node`, and `notification_node`. Watch for ready messages from both models and for
capture errors. `Ctrl-C` stops the launcher and its children.

In another shell with the same ROS environment and `ROS_DOMAIN_ID`:

```bash
source /opt/ros/jazzy/setup.bash
cd ~/joshua-av-test/joshua_main
./ros2_node_wrapper.sh av_check --config "$PWD/pi5_usb.pbtxt" \
  --seconds 10 --output "$PWD/av-check-01"
```

The output directory must be new. It contains:

- `audio.wav`: up to the requested number of seconds of PCM audio.
- `camera.png`: latest decoded frame.
- `events.json`: up to 1,000 inference/status events.
- `summary.json`: frame counts, audio RMS, audio gaps, event types, and pass/fail.

A passing check requires audio, a camera frame, and results from both inference
nodes, without malformed samples or audio gaps. It verifies transport and model
execution, not detection accuracy. Play the WAV and inspect the image to verify
that the selected devices are correct; review background noise and lighting.
A discontinuity produces a failed check rather than silently treating the WAV
as uninterrupted. Re-run after devices and inference have stabilized.

Topics default to `/audio/pcm`, `/camera/image`, `/audio/events`, `/video/events`.
Use `ros2 topic echo /audio/events` and `ros2 topic echo /video/events` to inspect
classification and detection output. Audio is a protobuf envelope in a ROS byte
array, not a raw WAV stream. See [ros2/README.md](../ros2/README.md#audio-publishing).

## Processing and events

YAMNet receives bounded contiguous PCM windows, downmixes stereo/multichannel to
mono, and evaluates the bundled model's 15,600-sample input with roughly 50%
overlap. Packet gaps, source changes, stream restarts, and stale audio reset the
window and cry state. ALSA performs requested rate conversion through its plug
device; the node rejects a rate other than 16 kHz.

Video inference keeps only the latest image and runs at the configured rate
(default 2 Hz). Output includes object labels/scores/normalized yxyx boxes and a
motion fraction, calculated from downscaled grayscale frame differences. Motion
is scene-wide, including caregivers, exposure changes, and camera shake.

Events have `schema_version`, `event_id`, `source`, `event_type`,
`observed_at_ns`, and event-specific evidence. Event types include
`audio_classification`, `cry_started`, `cry_ended`, `video_detection`,
`input_stale`, `input_restored`, and `inference_error`. Event subscriptions use
reliable QoS; input subscriptions use best effort and bounded history. Inputs
older than two seconds are rejected. Nodes report a stale source after three
seconds without fresh input; the optional Home Assistant bridge below handles phone delivery.

Detection thresholds, topics, model paths, and thread counts live in the protobuf
preset. Model paths can override the bundled model only with the same input,
output, and label-file contract. The experimental wrappers intentionally do not
use the actuator-oriented `InferenceHost`/`ActionCommand` API.

## Hardware-free checks

`//ros2:monitor_processor_test` tests PCM window continuity, image stride/color
handling, and real bundled model execution. `//ros2:monitor_pipeline_test`
launches the audio and video inference binaries as separate ROS processes,
publishes synthetic PCM/images, and checks both results and stale-source events.
It contains no sensor configuration and opens no hardware.

## Private live video and audio (Tailscale)

The `MEDIA_STREAM_NODE` utility subscribes to the existing camera and PCM topics.
It does not open either device. GStreamer encodes H.264 baseline video and Opus
sound once; the bundled, checksum-pinned MediaMTX server serves a WebRTC player.
Both inference processes remain independent. This phase does not deliver phone
notifications or record a rolling history.

Install streaming dependencies on the Ubuntu 24.04 Pi:

```bash
sudo apt-get update
sudo apt-get install -y libasound2t64 python3-gi \
  gir1.2-gstreamer-1.0 gir1.2-gst-plugins-base-1.0 \
  gstreamer1.0-tools gstreamer1.0-plugins-base gstreamer1.0-plugins-good \
  gstreamer1.0-plugins-bad gstreamer1.0-plugins-ugly gstreamer1.0-rtsp
```

MediaMTX is included in `joshua_pkg`; no separate server installation or model
runtime is needed by the streaming node. The GStreamer libraries and GI Python
bindings come from Ubuntu and must match the package's Python version.

With Tailscale connected on both devices, start the updated package with the
updated `pi5_usb.pbtxt`. The launcher starts six nodes, including streaming and notifications.
The node discovers its address using `tailscale ip -4` and prints the player URL:

```text
http://<Pi-Tailscale-IPv4>:8889/baby/
```

If capture/inference from an older package is already running, you can start
only the new utility from the newly extracted package instead:

```bash
source /opt/ros/jazzy/setup.bash
./ros2_node_wrapper.sh media_stream_node media_stream_node 5 "$PWD/pi5_usb.pbtxt"
```

Use the same `ROS_DOMAIN_ID` as capture. Choose either this command or the full
five-node launcher so only one streaming service owns the ports. `Ctrl-C` closes
the utility's encoder and MediaMTX child process.

Open that URL in iPhone Safari and tap the player's audio/unmute control. Keep
Tailscale connected when switching to LTE or another Wi-Fi network. The player
and media sockets bind only to the Tailscale address. HTTP is carried inside the
encrypted Tailscale tunnel; there is no public HTTP listener or router port
forwarding. Tailscale identity and tailnet grants/ACLs control viewer access;
there is no separate player password. Restrict access to the intended viewers.
Allow TCP 8889 and UDP/TCP 8189 to the Pi through those grants and any host
firewall. RTSP ingest binds to localhost only on TCP 8554. These ports and the
stream name are configurable in `utilities.media_streams`.

The same URL works at home and away while Tailscale is connected. A restrictive
network may cause Tailscale to relay traffic, affecting latency and throughput.
No exit node, subnet router, public TURN server, or public DNS is needed for this
private setup. Reconnect the player after a streaming restart or network change.

The default is 640x480 at up to 15 FPS and a 1500 kbps H.264 target bitrate.
The Pi 5 encodes in software. Keep raw ROS traffic on the Pi; the phone receives
only the compressed stream. Set `listen_address: "127.0.0.1"` for local synthetic
tests, or an explicit Tailscale IPv4 address to disable automatic discovery.
Other listener addresses are rejected. For container deployment, use host
networking so the Tailscale address exists inside the container network namespace.

The streaming worker has one pending video frame and at most 200 ms of queued
PCM. Old frames are discarded; audio timestamp gaps are marked discontinuous.
Source timestamps share one pipeline timeline. Camera timestamps currently mark
publication, not exposure, so exact hardware audio/video synchronization is not
promised. Input older than 500 ms is dropped. If a configured source stops for
three seconds, the encoder disconnects until fresh inputs return. The worker
restarts the encoder/server on failures with a bounded retry delay.

Inspect service transitions:

```bash
ros2 topic echo /media/events --field data
```

Events include `waiting_for_input`, `starting`, `streaming`, `input_stale`, and
`stream_error`. A streaming state describes the local encoder, not proof of a
connected phone. Test Safari over home Wi-Fi and then LTE, check audio/video
alignment with a visible clap, and observe Pi CPU/temperature during inference.
If playback stalls, check Tailscale connectivity, the printed address, firewall
rules, and these events before reducing bitrate/FPS in the preset.

Hardware-free coverage exercises bounded queues, private listeners, real H.264
and Opus encode/decode, source loss/recovery, and media-server crash recovery:

```bash
bazel test --config=u22 --config=x86-base \
  //ros2/utils:media_stream_test
```

The decoder test additionally needs `gstreamer1.0-libav` (included in dev images).

## Home Assistant dashboard and iPhone notifications

The monitor package includes `home_assistant_setup` and `notification_node`.
See [the Home Assistant deployment guide](../deploy/home_assistant/README.md)
for Docker setup, private HTTPS through Tailscale Serve, camera registration,
and connecting both iPhones. The Home Assistant app is published by Nabu Casa,
Inc.; a Cloud subscription is not required for this Tailscale setup.

`utilities.notifications` configures the utility bridge's event subscriptions,
local Home Assistant URL, credential-file environment name, timeout, freshness,
movement persistence, and cooldown. It never owns capture devices. It uses a
secret-protected, local-only webhook, not a Home Assistant user access token.
The generated deployment keeps that secret outside the repository.

The bridge subscribes reliably to audio/video events and periodic media status.
It forwards cry starts, sustained movement, failures, and recovery, coalescing
ordinary status to a five-second heartbeat. Alert delivery uses a bounded queue,
expiry, retries, and event IDs. `/notification/events` reports HTTP acceptance,
errors, and expiration; acceptance does not prove phone delivery. The Home
Assistant dashboard marks bridge status offline after 30 seconds without a
heartbeat and invalidates crying/movement values when their input is stale.

Automatic phone alerts start disabled. Register each Companion app, enter its
`notify.mobile_app_*` action on the dashboard, send a manual test, then enable
Notifications. Movement has a separate switch. Notification taps open the
live dashboard. Neither motion nor crying classifications are validated danger
detectors. Test delivery separately from model accuracy.

The supplied six-node preset expects this environment variable before launch:

```bash
export JOSHUA_HA_WEBHOOK_FILE="$HOME/joshua-home-assistant/webhook.secret"
```

Missing credentials leave the bridge reporting delivery errors while capture,
inference, and streaming continue. The bridge reads the secret file on each
attempt, so correcting it does not require restarting capture. Do not commit
or print its contents. Deployment and package upgrades must preserve the
Home Assistant `config/` directory, accounts, and generated credential.

For automatic startup, deployment updates, and the independent Home Assistant
health watchdog, see [persistent startup](../deploy/home_assistant/README.md#persistent-startup-and-updates).
