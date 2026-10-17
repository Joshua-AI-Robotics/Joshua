# Home Assistant deployment

This directory supplies a private Home Assistant Container deployment alongside
Joshua on Ubuntu ARM64 or x86_64. It uses an explicit go2rtc sidecar so its WebRTC
listener and ICE candidates can be restricted to the Tailscale interface. Video
and audio are relayed from Joshua's existing MediaMTX RTSP feed; no second camera
or microphone capture process is started.

Run the packaged `home_assistant_setup` utility to create a NEW deployment
folder. It reads the media endpoint from the Joshua protobuf preset. Run
`--help` for options. It never overwrites an existing deployment.

```bash
./ros2_node_wrapper.sh home_assistant_setup \
  --config "$PWD/pi5_usb.pbtxt" --output "$HOME/joshua-home-assistant" \
  --tailscale-ip "$(tailscale ip -4)"
cd ~/joshua-home-assistant
sudo docker compose up -d
sudo tailscale serve --bg --https=443 http://127.0.0.1:8123
```

Install Docker Engine and Compose first (`sudo apt install docker.io
docker-compose-v2` on Ubuntu 24.04). The deployment has no privileged containers,
USB device passthrough, or public port forwarding. Home Assistant and go2rtc's
management API bind loopback; go2rtc media uses Tailscale TCP/UDP 18555. Tailscale
Serve carries HTTPS/WebSocket signaling on 443. If you restrict Tailscale ACLs,
permit both 443/TCP and 18555/TCP+UDP for each viewer.

## Connect the phones

1. Open `https://<Pi's DNSName from tailscale status --json>` with Tailscale on.
2. Complete Home Assistant onboarding and create your owner account. No
   Home Assistant Cloud subscription is required. Create a separate account
   for the second person in Settings → People → Users.
3. Install **Home Assistant**, published by Nabu Casa, Inc., on each iPhone.
   Choose Connect to my Home Assistant server → Enter address manually, enter
   the same HTTPS URL, sign in, and allow notifications. Name each phone uniquely.
4. Select **Baby Monitor** in the sidebar. You can make this your default dashboard
   in your profile settings.

The renderer seeds the HTTP settings store for the pinned Home Assistant version
before first startup. This keeps the listener on loopback throughout onboarding;
there is no temporary YAML migration to confirm. Subsequent settings belong to
Settings → System → Network. The renderer refuses existing directories so it
cannot overwrite accounts or user settings. Recheck the HTTP storage format
and private listener when updating the pinned Home Assistant version.

## Add the camera

Before adding the camera, open Settings → System → Network and set both the
Home Assistant URL and local network URL to the same Tailscale HTTPS address
used by the phones. Disable automatic local URL selection if shown. Generic
Camera's setup preview constructs an absolute HLS URL; an automatically detected
HTTP/LAN address cannot serve that preview through this private HTTPS deployment.
After changing the URLs, close any old camera setup dialog and start a fresh one.
A preview left open without playback can expire after 60 seconds and log
`Stream is not configured for format 'hls'`.

Under Settings → Devices & services → Add integration → Generic Camera:

- Stream source: **RTSP_URL_PLACEHOLDER** (inside the host-network container).
- Still image URL: leave blank.
- RTSP transport: TCP.
- Leave credentials blank; this feed is accessible only on the Pi's loopback.
- Confirm the preview, name the device **Baby Monitor**, and set the camera
  entity ID to **camera.baby_monitor** in the entity settings.

Home Assistant proxies WebRTC signaling to go2rtc. The iPhone connects over
Tailscale for media. H.264/Opus are forwarded without a second video encode.
Tap the camera and unmute; iOS can require a user gesture for audio. Do not
assume continuous audio after locking the phone or backgrounding the app.

## Start the notification bridge

The generated `webhook.secret` is a private local credential, also stored in
Home Assistant's secrets file. Keep both files out of Git, chat, and screenshots.
The webhook accepts POSTs from local addresses only. The packaged node reads its
path from the environment named in the protobuf preset:

```bash
export JOSHUA_HA_WEBHOOK_FILE="$HOME/joshua-home-assistant/webhook.secret"
# While the existing capture, inference and media nodes are already running:
./ros2_node_wrapper.sh notification_node notification_node 6 "$PWD/pi5_usb.pbtxt"
```

Alternatively, export that variable before launching the full six-node monitor.
Do not start a second full monitor alongside existing capture nodes.

In Settings → Tools → Actions, find each `notify.mobile_app_*` action. On the
Baby Monitor dashboard, enter the comma-separated action names into **Phone
notification actions**. Press **Send test notification**. Once both phones
receive it, enable **Notifications**. Enable **Movement notifications** only if
wanted. Notifications open `/baby-monitor/live` in the app.

Normal alert text travels through Home Assistant's push service and Apple/Google
infrastructure. Opening the dashboard requires Tailscale; text delivery does not.
No camera snapshots are uploaded by this configuration. Cry-start alerts request
the default critical sound at full volume on iPhone; allow Critical Alerts in
iOS notification settings for Home Assistant. Movement, outage, recovery, and
dashboard test notifications use ordinary sounds. Critical alerts do not repeat
automatically while crying continues; the existing episode/cooldown rules apply.

## Operations and acceptance checks

```bash
sudo docker compose logs --tail=80 homeassistant go2rtc
sudo docker compose restart homeassistant
```

Test both phones over Wi-Fi and cellular: live video, unmuted audio, a locked-screen
manual push, and opening the dashboard from the push. Then enable automatic
notifications and test a cry event separately from actual model accuracy.
The bridge suppresses duplicates, expires old events, and reports input state.
HTTP acceptance is not proof that a phone displayed a notification. Calling a
phone action directly in Tools does not populate the dashboard's recipient
field. Save that field and confirm **Joshua notification configured** is on.

Monitor status describes the whole graph: **online** requires every configured
input to be healthy; **degraded** means some inputs have failed; **offline**
means no healthy inputs or no bridge heartbeat for 30 seconds. Individual
cry/movement readings become unavailable when their own input is stale.

Home Assistant runs an independent watchdog every 15 seconds. With notifications
enabled and recipients configured, an offline/degraded state lasting 15 seconds
sends one outage alert, even if the launcher and notification node both stopped.
Complete heartbeat loss is normally reported within 45–70 seconds. After ten
seconds of healthy operation, it sends recovery and resets outage suppression.
Short outages repaired before that window do not generate outage pushes.

## Persistent startup and updates

The archive includes all six nodes; installing it does not automatically enable
capture. After reviewing the preset and setting up the webcam/microphone, keep
the editable preset outside the release directory and use a stable package
symlink. For example, on the Pi:

```bash
mkdir -p ~/joshua-config
# Set joshua-current to an extracted release's joshua_main directory first.
cp ~/joshua-current/joshua_monitor_main.runfiles/_main/config/config_preset/baby_monitor/pi5_usb.pbtxt \
  ~/joshua-config/pi5_usb.pbtxt
scripts=~/joshua-current/joshua_monitor_main.runfiles/_main/deploy/home_assistant
sudo bash "$scripts/install_services.sh" "$HOME/joshua-current" \
  "$HOME/joshua-config/pi5_usb.pbtxt" "$HOME/joshua-home-assistant" "$USER"
# Stop any older launcher and standalone media/notification services first.
sudo systemctl start joshua-monitor
sudo journalctl -u joshua-monitor -f
```

The installer enables persistent `joshua-home-assistant.service` and
`joshua-monitor.service`. It preserves HA configuration and credentials. The
launcher runs as the supplied user, reads the specified protobuf config, and
waits for local HA/Tailscale readiness. The installer enables user lingering so
logind does not remove Fast DDS shared memory when the last SSH session closes.
A failed child causes the graph to exit
and systemd to restart it; an explicit `systemctl stop joshua-monitor` stays
stopped until started or rebooted. Stopping the monitor leaves Home Assistant
running so its watchdog can report the outage. The services are separate for
this reason, although the archive carries everything needed to install them.

Build updates from the repository root:

```bash
TARGET=//launcher:joshua_pkg docker compose run --rm build-u24-arm64
scp dist/u24/arm64/joshua_pkg-0.2.3-u24-arm64.tar.gz "<SSH_USER>@<PI_LAN_IP>:~/"
```

Extract into a new release directory on the Pi. Stop `joshua-monitor`, switch
`~/joshua-current` to that release's `joshua_main` directory, and start the service.
Do not extract over a running package. Config-only changes need a service restart,
not a rebuild. For changes to these installer scripts, rerun the installer before
restarting. For HA template changes, back up and update only
`config/packages/joshua.yaml`, check HA configuration, then restart its container;
do not rerun `home_assistant_setup` over an existing deployment.

With the services installed, both Home Assistant and the complete Joshua pipeline
start after reboot. A single Pi power/network failure also stops Home Assistant's
watchdog; external uptime monitoring is needed to report that failure remotely.

Back up the entire deployment directory including `.storage` and private secrets.
It contains account credentials and must not be committed. Update the pinned image
versions deliberately and check config before restarting. Stop with
`sudo docker compose down`; this preserves config. Revoke private web access with
`sudo tailscale serve reset` only if this is the Pi's sole Serve service.
