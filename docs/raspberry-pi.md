# Raspberry Pi 5 (4 GB) — vision + sound engine

Run Vision soterrada and the RUMOR standalone on one Pi 5 with 4 GB of RAM. The phone or laptop is the control surface. The Pi does not need a keyboard after the first calibration.

This is written for Raspberry Pi OS **64-bit Bookworm with desktop**, a USB webcam, and a USB audio interface. The official 27 W power supply matters: a weak supply throttles the Pi and the audio glitches.

## What actually runs headless

| Process | Needs a screen? | How you control it |
| --- | --- | --- |
| `remote` (Socket.IO hub) | No | Browser on another machine: `http://<pi>:8080` |
| RUMOR standalone (synth + PaulXStretch) | It opens a window once | After it is linked, the websocket page drives parameters, presets, and the serial sensor |
| Vision soterrada | Yes, while you lock the object | OpenCV window. There is no vision page on the websocket hub |

The websocket hub already in `remote/` talks to the JUCE app. It does not draw the camera, the faders, or **Fijar**. Vision also does not drive the synth yet: it sends OSC to `127.0.0.1:8000`, and the standalone does not listen on that port. Both can run on the Pi today; the proximity value does not move a synth parameter until that bridge exists.

Vision can keep running after you unplug HDMI if the desktop session stays logged in (or you use VNC). **Fijar** is not saved to disk. A reboot returns to the largest white blob until you lock the object again.

Try MediaPipe on the first install. If the wheel is missing or `H` drives the board into swap, drop it. Blob tracking does not need it.

## What to plug in

- Raspberry Pi 5, 4 GB, official 27 W PSU, active cooler.
- microSD, 32 GB or larger. A2 / application-class.
- USB webcam that shows up as a UVC device (`/dev/video0`). The tracker opens the camera with V4L2. A Pi Camera Module on the CSI port is not what this code opens.
- USB audio interface for the exciters and the room. The analog headphone jack is a last resort.
- Arduino NANO on USB for the capacitive sensor, if that part of the piece is in this build.
- HDMI and a keyboard for the first boot and the first **Fijar**. After that, VNC or another computer on the same network is enough.

## 1. Flash the card

Use [Raspberry Pi Imager](https://www.raspberrypi.com/software/).

1. Device: **Raspberry Pi 5**.
2. OS: **Raspberry Pi OS (64-bit)**. The desktop image, not Lite. Lite has no display, and both the standalone and the vision window need one.
3. Storage: the microSD card.
4. Edit the customization before writing:
   - Hostname: `rumor`
   - Username and password.
   - Wi-Fi, if the Pi will not use Ethernet. Ethernet is the better choice in the room.
   - Locale and keyboard.
   - Enable SSH. Password authentication is enough on a private network.
5. Write, then boot the Pi once with HDMI so it finishes the first-boot resize.

## 2. First shell

From another computer:

```bash
ssh <user>@rumor.local
```

If mDNS does not resolve, use the address from the router. Then:

```bash
sudo apt update
sudo apt full-upgrade -y
sudo reboot
```

SSH back in and give the account the device groups. `video` is the webcam, `audio` is the interface, `dialout` is the Arduino, `render` and `input` are the desktop session.

```bash
sudo usermod -aG video,audio,dialout,render,input,plugdev "$USER"
```

Log out and back in so the groups apply (`exit`, then `ssh` again). Check them with `groups`.

## 3. Swap, before the compile

A Release build of the standalone plus PaulXStretch will kill `cc1plus` on 4 GB if the compile is too wide. The Linux CI job uses `-j2` for that reason. Add a swap file first. Pi OS already has a small zram device; this file sits beside it.

```bash
sudo fallocate -l 2G /swapfile
sudo chmod 600 /swapfile
sudo mkswap /swapfile
sudo swapon /swapfile
echo '/swapfile none swap sw 0 0' | sudo tee -a /etc/fstab
free -h
```

You want to see swap with 2 GB available before `cmake --build`.

## 4. Packages

These are the same libraries the Linux CI job installs, plus the camera and a browser-less Node 20. The remote hub declares `"node": ">=20"`. Debian's `nodejs` package on Bookworm is older.

```bash
sudo apt install -y --no-install-recommends \
  git cmake ninja-build pkg-config build-essential \
  libasound2-dev libfreetype6-dev \
  libx11-dev libxcomposite-dev libxcursor-dev libxext-dev \
  libxinerama-dev libxrandr-dev libxrender-dev \
  libgl1-mesa-dev libcurl4-openssl-dev libssl-dev \
  libgtk-3-dev libwebkit2gtk-4.1-dev \
  python3 python3-venv python3-pip \
  libgl1 libglib2.0-0 v4l-utils alsa-utils \
  curl ca-certificates

curl -fsSL https://deb.nodesource.com/setup_20.x | sudo -E bash -
sudo apt install -y nodejs
node -v
```

`node -v` should print v20 or newer.

## 5. Clone

```bash
git clone https://github.com/rafaelbecks/rumor.git ~/rumor
cd ~/rumor
```

Use the forge URL you actually push to if it is not GitHub.

## 6. Build the sound engine

JUCE is fetched into `deps/` on the first configure if `deps/JUCE` is not already there. That download is large. `-j2` is deliberate.

```bash
cd ~/rumor
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DRUMOR_WITH_PAULXSTRETCH=ON \
  -DRUMOR_BUILD_PLUGIN=ON \
  -DRUMOR_BUILD_TESTS=OFF
cmake --build build --target Rumor_Standalone -j2
```

The binary is:

```text
~/rumor/build/Rumor_artefacts/Release/Standalone/RUMOR
```

A CI zip (`RUMOR-linux-aarch64.zip` on the `latest` GitHub release) is worth trying if you would rather not compile. The Linux job is allowed to fail, so the zip is not guaranteed. If it is missing, the build above is the path.

Confirm the sound card:

```bash
aplay -l
arecord -l
```

Remember the card name. The standalone asks for it the first time the window opens. Pick the USB interface, 48000 Hz, and a buffer of 256 or 512. A buffer of 128 is what `config/mapping.json` asks for and is tight on this board once vision is also running.

## 7. Vision, MediaPipe first

Bookworm's `python3` is 3.11, which is new enough. Install the full set first, hands included. If the MediaPipe wheel is missing for aarch64, or the first `H` pushes the 4 GB board into swap, reinstall without it. Blob tracking still runs.

```bash
cd ~/rumor/vision
python3 -m venv .venv
source .venv/bin/activate
pip install --upgrade pip
pip install -r requirements.txt
python main.py --self-test
```

Hands stay off until you press `H` or pass `--hands`. The model downloads into `vision/models/` the first time. Watch `free -h` while that happens.

If `pip install -r requirements.txt` fails on `mediapipe`:

```bash
pip install "opencv-python>=4.9" "python-osc>=1.8.3" "numpy>=1.26"
python main.py --self-test
```

See the camera:

```bash
v4l2-ctl --list-devices
```

You want `/dev/video0` (or another index you will pass as `--camera`). If the list is empty, the webcam is not UVC, the cable is power-only, or the user is not in the `video` group yet.

## 8. Websocket hub

Chrome refuses port 6000 (`ERR_UNSAFE_PORT`). The server's default is 6000, so set 8080.

```bash
cd ~/rumor/remote
npm ci
PORT=8080 HOST=0.0.0.0 npm start
```

From a laptop on the same network, open `http://<pi-ip>:8080`. Leave this running.

## 9. Start the three processes

The desktop has to be logged in. On Bookworm that session is Wayland (`labwc`) with XWayland for the JUCE and OpenCV windows. From an SSH shell, point the GUI apps at that session. Replace `1000` if `id -u` prints something else.

```bash
export DISPLAY=:0
export XDG_RUNTIME_DIR=/run/user/$(id -u)
export WAYLAND_DISPLAY=wayland-0
```

If a window never appears, install and enable VNC (`sudo raspi-config`, Interface Options → VNC) and use that instead of guessing the display name. `echo $XDG_RUNTIME_DIR` inside a local terminal on the HDMI desktop shows the right runtime directory.

Three terminals, or three `tmux` panes.

Hub:

```bash
cd ~/rumor/remote
PORT=8080 HOST=0.0.0.0 npm start
```

Sound engine:

```bash
export DISPLAY=:0
export XDG_RUNTIME_DIR=/run/user/$(id -u)
export WAYLAND_DISPLAY=wayland-0
cd ~/rumor
./build/Rumor_artefacts/Release/Standalone/RUMOR
```

In the standalone window, audio device first, then the serial tab. Set the remote URL to:

```text
ws://127.0.0.1:8080
```

and press **vincular**. The browser page should list the device. From then on, parameters and the sensor mode (`serial` / `websocket` / off) can be changed from the laptop. The window can stay in the background. Closing it stops the audio.

Vision, still on the Pi's desktop or over VNC, the first time:

```bash
export DISPLAY=:0
export XDG_RUNTIME_DIR=/run/user/$(id -u)
export WAYLAND_DISPLAY=wayland-0
cd ~/rumor/vision
source .venv/bin/activate
python main.py --camera 0 --width 640 --height 480 \
  --host 127.0.0.1 --port 8000 \
  --software-exposure \
  --calibration calibration.json
```

`640x480` is the right starting size on 4 GB. Move to `1280x720` only after `free -h` still shows free RAM while audio is running. `--software-exposure` avoids the exposure probe, which grabs extra frames and often does nothing useful on a UVC webcam.

In the vision window: frame the object, tune the faders, stand clear, **Fijar**, click the object. **Guardar** writes the faders only. The lock itself lasts until the process exits.

## 10. Start on boot

Autologin to the desktop (`sudo raspi-config` → System Options → Boot / Auto Login → Desktop Autologin). Then a user service for each process. Do this only after a manual start has worked.

`~/.config/systemd/user/rumor-remote.service`:

```ini
[Unit]
Description=RUMOR websocket hub
After=network-online.target

[Service]
WorkingDirectory=%h/rumor/remote
Environment=PORT=8080
Environment=HOST=0.0.0.0
ExecStart=/usr/bin/npm start
Restart=on-failure
RestartSec=3

[Install]
WantedBy=default.target
```

`~/.config/systemd/user/rumor-audio.service`:

```ini
[Unit]
Description=RUMOR standalone
After=graphical-session.target rumor-remote.service

[Service]
WorkingDirectory=%h/rumor
Environment=DISPLAY=:0
Environment=WAYLAND_DISPLAY=wayland-0
ExecStart=%h/rumor/build/Rumor_artefacts/Release/Standalone/RUMOR
Restart=on-failure
RestartSec=3

[Install]
WantedBy=graphical-session.target
```

`~/.config/systemd/user/rumor-vision.service`:

```ini
[Unit]
Description=Vision soterrada
After=graphical-session.target

[Service]
WorkingDirectory=%h/rumor/vision
Environment=DISPLAY=:0
Environment=WAYLAND_DISPLAY=wayland-0
ExecStart=%h/rumor/vision/.venv/bin/python main.py --camera 0 --width 640 --height 480 --host 127.0.0.1 --port 8000 --software-exposure --calibration %h/rumor/vision/calibration.json
Restart=on-failure
RestartSec=3

[Install]
WantedBy=graphical-session.target
```

Enable them as your user, not as root:

```bash
systemctl --user daemon-reload
systemctl --user enable --now rumor-remote.service
loginctl enable-linger "$USER"
```

Leave the audio and vision units **disabled** until you have linked the websocket once and locked the object once by hand. A service that starts vision before the desktop is up will exit immediately, and a service that restarts vision will drop the lock every time.

```bash
systemctl --user enable rumor-audio.service rumor-vision.service
```

After the next reboot, check:

```bash
systemctl --user status rumor-remote.service rumor-audio.service rumor-vision.service
```

`loginctl enable-linger` is what lets the hub start at boot even before you log in. The two GUI units still wait for the desktop session.

## 11. Memory

With the desktop, the standalone, and vision at 640×480, expect roughly:

| Piece | Working set |
| --- | --- |
| Desktop session | 400–700 MB |
| RUMOR standalone | 200–500 MB, more while PaulXStretch has a long buffer |
| Vision, 640×480, no MediaPipe | 150–300 MB |
| Vision with hands on | add a few hundred MB; watch swap |
| Node hub | under 80 MB |

Leave headroom. If `free -h` shows almost no available memory and the audio crackles, drop the camera to 640×480, raise the audio buffer, and do not open a browser on the Pi. The control page belongs on the laptop.

Watch it while both are running:

```bash
free -h
vcgencmd get_throttled
```

`throttled=0x0` means the supply and the cooler are fine. Anything else is undervoltage or thermal limiting.

## 12. Checks before you leave it

1. `aplay -l` shows the USB interface, and the standalone's audio settings point at it. Play a preset from the websocket page and hear it on the interface, not only on HDMI.
2. `http://<pi>:8080` connects, **vincular** stays up, and moving a fader on the page moves the sound.
3. The Arduino, if used, appears as `/dev/ttyUSB0` or `/dev/ttyACM0` and the serial listen mode in the remote page shows numbers.
4. `python main.py --self-test` inside `vision/.venv` prints `self-test passed`.
5. With the object in frame, `/radial/distance` changes when something crosses the gold circle. Vision only sends UDP; it does not bind the port, so this listener can run beside it:

```bash
cd ~/rumor/vision && source .venv/bin/activate
python - <<'PY'
from pythonosc.dispatcher import Dispatcher
from pythonosc.osc_server import BlockingOSCUDPServer
dispatcher = Dispatcher()
dispatcher.map("/radial/distance", lambda *args: print("distance", args[1:]))
BlockingOSCUDPServer(("127.0.0.1", 8000), dispatcher).serve_forever()
PY
```

Nothing is printed until vision is running with `--host 127.0.0.1 --port 8000`. The number is the synth's problem only after something in the standalone subscribes to that address. That subscriber is not in the app yet.
6. Reboot once. The hub should come back. The standalone and vision come back only if their user services are enabled and desktop autologin worked. Open the vision window over VNC and **Fijar** again if the tracker was restarted.
