# Experimental Pi audio/video bring-up

This branch adds independent YAMNet and video inference processes on top of the
reusable ALSA audio pipeline. Both publish versioned JSON events in ROS
`std_msgs/String`. Notification delivery, browser streaming, and rolling media
recording are later work. `av_check` is a bounded diagnostic recording, not the
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
scp dist/u24/arm64/joshua_pkg-*-u24-arm64.tar.gz hmoon@172.20.3.11:~/
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

Four processes should start: `camera_publisher`, `audio_publisher`, `yamnet_node`,
and `video_inference_node`. Watch for ready messages from both models and for
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
seconds without fresh input; there is no phone delivery in this milestone.

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
