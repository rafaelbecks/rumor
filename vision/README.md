# RUMOR vision

Real-time tracker for a white or translucent anchor. It measures how close the surrounding blobs are, in a circle around that anchor, and streams the result over OSC.

Python 3.10 or newer. The system `python3` on macOS is often 3.9 — use a newer interpreter (Homebrew `python3.12` works).

## Run

```bash
cd vision
python3.12 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt

python main.py --self-test
python main.py
```

Camera index, resolution, OSC destination, and the calibration file:

```bash
python main.py --camera 0 --width 1280 --height 720 \
  --host 127.0.0.1 --port 8000 \
  --calibration calibration.json
```

Other flags: `--hands` (MediaPipe), `--debug`, `--software-exposure` (ignore the camera's exposure even if the driver accepts it), `--no-osc`. `--headless` skips the window and prints the fps once a second. `--frames 150` stops after 150 frames and prints the average. `python main.py --help` prints the same key and OSC reference the app prints on startup.

On macOS, if the window stays black, allow camera access for the terminal or Cursor in System Settings.

If `mediapipe` has no wheel for your machine, install the rest and keep going. Blob tracking does not need it:

```bash
pip install opencv-python python-osc numpy
```

## Keys

| Key | Action |
| --- | --- |
| `P` | Fijar: a square on every detected blob. Click one to freeze it |
| `D` | Mascara: the feed, the white mask, range rings, and distance lines |
| `S` | Guardar: write the faders to `calibration.json` |
| `L` | Reload that file |
| `H` | MediaPipe hands on or off |
| `Q` / `ESC` | Quit |

The top bar is the camera selector (`<` `>`), the OSC host and port, and the calibration faders. **Fijar** (or `P`) draws a square on each blob. Click a square to freeze that object as the anchor and to save the current frame as the empty reference. The frozen object stays where you clicked it; it does not jump to another blob. Click the empty background to release it. **Guardar** (or `S`) writes the faders to `calibration.json`. It does not store the object. **Foco** is autofocus and starts off.

The live view always draws the anchor contour and the radial zone. Debug adds the mask and the measurements.

## Calibration

1. Frame the white or translucent object.
2. Watch the status line. `expo HW` means the camera took the control. `expo SW` means the driver locked it and the Expo slider is applied in software (same for Gan / ISO).
3. Press `D`. Raise **Min V** and lower **Max S** until the white mask contains the anchor and not the room. Colored highlights are rejected with a LAB chroma gate tied to Max S.
4. Stand clear of the object, press **Fijar**, and click the blob you want. That click freezes the object in place and saves the frame as the empty reference. It will not switch to another blob. Click the background to release it. With no lock, the largest white blob is the anchor and neighbors are color blobs again.
5. **Foco** is autofocus. It starts off so the lens does not hunt.
6. Set **Zona** to the perimeter you care about. **A min** / **A max** still limit which white blobs can be chosen automatically.
7. Press **Guardar** or `S`. That writes the faders only.

**Suave** eases the distance sent over OSC. `0` follows the measurement immediately. Higher values lerp it so a new blob does not jump the number. With an object fixed, the view draws the object's own circle, the gold zone circle, and an orange circle at the current radial distance.

**Mascara** shows the white mask beside the image at the same window size.

**Contrast** is 1.0–3.0, **Brightness** is −100–100, **Gamma** is 0.30–3.00. Gamma above 1.0 lifts the midtones. Exposure 50 and Gain 0 are neutral when the adjustment is in software.

## Distance

`0.0` is contact with the anchor's outer edge. `1.0` is the zone circle. Objects outside the circle are ignored. `/radial/frame` reports that same gap as a fraction of the longer side of the picture, so a value of `0.10` means the object is a tenth of the frame away from the anchor.

```text
euclidean  = hypot(nearest_point - anchor_centroid)
radius     = anchor radius in that direction
gap        = max(0, euclidean - radius)
normalized = gap / max(1, zone_radius - radius)
```

```text
                 zone edge = 1.0
              ·················
            ·                   ·
           ·     neighbor        ·
           ·         |           ·
            ·        | gap      ·
              ·   (anchor)    ·
                ·············
                  contact = 0.0
```

Angle is degrees from the anchor centroid toward that nearest point: `0°` image-right, `90°` down, `180°` left, `270°` up. Anchor position is normalized with `(0, 0)` at the top-left of the frame.

The largest white blob is the anchor. Smaller white blobs, colored or dark blobs, and (when enabled) MediaPipe hands are the neighbors. A hand replaces a blob at the same spot so one person is not counted twice. Bodies that are not detected as hands still show up as blobs. At most 16 neighbors are sent, closest first.

## OSC

UDP, default `127.0.0.1:8000`, every frame.

| Address | Arguments |
| --- | --- |
| `/anchor/position` | `norm_x`, `norm_y` |
| `/radial/nearest` | `normalized_distance`, `raw_pixel_distance`, `angle_degrees` |
| `/radial/distance` | `normalized_distance` |
| `/radial/frame` | `frame_distance` |
| `/radial/objects` | `count`, `dist_1`, `dist_2`, … |

`raw_pixel_distance` is the gap in pixels (`0` when touching). `/radial/distance` is the normalized gap by itself. `/radial/frame` is that gap divided by the longer side of the frame. The `dist_*` values are normalized gaps. A lost anchor sends `-1 -1`. An empty zone sends `-1` on the distance addresses, `-1 -1 -1` on `/radial/nearest`, and `count 0`.

Listen locally:

```bash
python - <<'PY'
from pythonosc.dispatcher import Dispatcher
from pythonosc.osc_server import BlockingOSCUDPServer

dispatcher = Dispatcher()
dispatcher.set_default_handler(lambda address, *args: print(address, args))
BlockingOSCUDPServer(("127.0.0.1", 8000), dispatcher).serve_forever()
PY
```

The first time you press `H`, the app downloads the MediaPipe hand model into `vision/models/` (gitignored) and caches it.
