"""Calibration values, JSON presets, camera exposure, and the slider panel."""

from __future__ import annotations

import json
from dataclasses import asdict, dataclass, replace
from pathlib import Path

import cv2
import numpy as np

WINDOW_NAME = "Vision soterrada"
NOTE = "RUMOR vision calibration. Press S in the app to overwrite this file."

# OpenCV trackbars are integers that start at 0. These labels are the slider
# names; the mapping below turns them into the units the tracker uses.
_TRACKBARS: dict[str, int] = {
    "Exposure": 100,
    "Gain / ISO": 100,
    "Contrast": 200,
    "Brightness": 200,
    "Gamma": 270,
    "Min Brightness": 255,
    "Max Saturation": 255,
    "Min Blob Area": 4000,
    "Max Blob Area": 500,
    "Radial Zone": 860,
}


@dataclass
class Calibration:
    """Tunable thresholds. Integer fields are the units shown in the HUD."""

    exposure: int = 50
    gain: int = 0
    contrast: float = 1.0
    brightness: int = 0
    gamma: float = 1.0
    min_brightness: int = 185
    max_saturation: int = 50
    min_blob_area: int = 1000
    max_blob_area: int = 300000
    max_radial_zone: int = 360
    smoothing: int = 60

    def clamped(self) -> Calibration:
        minimum_area = _as_int(self.min_blob_area, 1000, 0, 40000)
        maximum_area = _as_int(self.max_blob_area, 300000, 1000, 500000)
        if maximum_area <= minimum_area:
            maximum_area = min(500000, minimum_area + 1000)
        return Calibration(
            exposure=_as_int(self.exposure, 50, 0, 100),
            gain=_as_int(self.gain, 0, 0, 100),
            contrast=round(float(np.clip(_as_float(self.contrast, 1.0), 1.0, 3.0)), 2),
            brightness=_as_int(self.brightness, 0, -100, 100),
            gamma=round(float(np.clip(_as_float(self.gamma, 1.0), 0.30, 3.0)), 2),
            min_brightness=_as_int(self.min_brightness, 185, 0, 255),
            max_saturation=_as_int(self.max_saturation, 50, 0, 255),
            min_blob_area=minimum_area,
            max_blob_area=maximum_area,
            max_radial_zone=_as_int(self.max_radial_zone, 360, 40, 900),
            smoothing=_as_int(self.smoothing, 60, 0, 100),
        )


@dataclass
class HardwareSupport:
    """Which camera controls actually change the image."""

    exposure: bool = False
    gain: bool = False


def positions_from_calibration(cal: Calibration) -> dict[str, int]:
    cal = cal.clamped()
    positions = {
        "Exposure": cal.exposure,
        "Gain / ISO": cal.gain,
        "Contrast": int(round((cal.contrast - 1.0) * 100)),
        "Brightness": cal.brightness + 100,
        "Gamma": int(round((cal.gamma - 0.30) * 100)),
        "Min Brightness": cal.min_brightness,
        "Max Saturation": cal.max_saturation,
        "Min Blob Area": int(round(cal.min_blob_area / 10)),
        "Max Blob Area": int(round(cal.max_blob_area / 1000)),
        "Radial Zone": cal.max_radial_zone - 40,
    }
    return {name: int(np.clip(value, 0, _TRACKBARS[name])) for name, value in positions.items()}


def calibration_from_positions(positions: dict[str, int]) -> Calibration:
    return Calibration(
        exposure=positions["Exposure"],
        gain=positions["Gain / ISO"],
        contrast=1.0 + positions["Contrast"] / 100.0,
        brightness=positions["Brightness"] - 100,
        gamma=0.30 + positions["Gamma"] / 100.0,
        min_brightness=positions["Min Brightness"],
        max_saturation=positions["Max Saturation"],
        min_blob_area=positions["Min Blob Area"] * 10,
        max_blob_area=max(1, positions["Max Blob Area"]) * 1000,
        max_radial_zone=positions["Radial Zone"] + 40,
    ).clamped()


def load_calibration(path: Path) -> tuple[Calibration, str]:
    """Load a preset. A missing or broken file falls back to the defaults."""

    if not path.exists():
        return Calibration(), f"No hay calibracion en {path}. Valores por defecto. S guarda."
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
        if not isinstance(data, dict):
            raise ValueError("calibration file must be a JSON object")
        current = asdict(Calibration())
        for key in current:
            if key in data:
                current[key] = data[key]
        cal = Calibration(**current).clamped()
    except (OSError, json.JSONDecodeError, TypeError, ValueError) as exc:
        return Calibration(), f"No se pudo leer {path} ({exc}). Valores por defecto."
    return cal, f"Calibracion cargada desde {path}"


def save_calibration(path: Path, cal: Calibration) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    payload = {"_note": NOTE, **asdict(cal.clamped())}
    path.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")


@dataclass
class Session:
    """Live controls. The calibration file stores the fader values only."""

    calibration: Calibration
    camera_index: int = 0
    host: str = "127.0.0.1"
    port: int = 8000
    pick: bool = False
    debug: bool = False
    hands: bool = False
    autofocus: bool = False
    status: str = ""


@dataclass(frozen=True)
class _Fader:
    key: str
    label: str
    low: float
    high: float
    decimals: int


_FADERS = (
    _Fader("exposure", "Expo", 0, 100, 0),
    _Fader("gain", "Gan", 0, 100, 0),
    _Fader("contrast", "Cont", 1.0, 3.0, 2),
    _Fader("brightness", "Brillo", -100, 100, 0),
    _Fader("gamma", "Gamma", 0.30, 3.0, 2),
    _Fader("min_brightness", "Min V", 0, 255, 0),
    _Fader("max_saturation", "Max S", 0, 255, 0),
    _Fader("min_blob_area", "A min", 0, 40000, 0),
    _Fader("max_blob_area", "A max", 1000, 500000, 0),
    _Fader("max_radial_zone", "Zona", 40, 900, 0),
    _Fader("smoothing", "Suave", 0, 100, 0),
)
_FADER_BY_KEY = {item.key: item for item in _FADERS}


@dataclass(frozen=True)
class _Hit:
    kind: str
    key: str
    rect: tuple[int, int, int, int]
    track: tuple[int, int, int, int] | None = None


class TopBar:
    """Compact bar: camera, OSC destination, and the calibration faders."""

    height = 108

    def __init__(self) -> None:
        self.focus: str | None = None
        self._edit = ""
        self._drag_key: str | None = None

    def layout(self, width: int) -> list[_Hit]:
        width = max(int(width), 360)
        hits: list[_Hit] = []
        y = 6
        h = 22
        x = 36
        hits.append(_Hit("button", "camera-prev", (x, y, 20, h)))
        x += 48
        hits.append(_Hit("button", "camera-next", (x, y, 20, h)))
        x += 58
        host_w = max(88, min(168, width - x - 390))
        hits.append(_Hit("field", "host", (x, y, host_w, h)))
        x += host_w + 4
        hits.append(_Hit("field", "port", (x, y, 52, h)))
        x += 58
        for key, button_w in (("pick", 52), ("debug", 74), ("hands", 58), ("save", 74)):
            if x + button_w > width - 8:
                break
            hits.append(_Hit("button", key, (x, y, button_w, h)))
            x += button_w + 4
        if x + 92 <= width - 8:
            hits.append(_Hit("check", "autofocus", (x, y, 92, h)))
        slot_w = max(64, (width - 16) // 5)
        for index, spec in enumerate(_FADERS):
            column = index % 5
            row = index // 5
            fx = 8 + column * slot_w
            fy = 34 + row * 24
            usable = slot_w - 8
            label_w, value_w = 46, 32
            track_w = usable - label_w - value_w
            if track_w < 16:
                label_w = max(20, usable - value_w - 16)
                track_w = max(12, usable - label_w - value_w)
            track = (fx + label_w, fy + 6, track_w, 10)
            hits.append(_Hit("fader", spec.key, (fx, fy, slot_w - 6, 22), track))
        return hits

    def draw(self, width: int, session: Session) -> np.ndarray:
        width = max(int(width), 360)
        image = np.full((self.height, width, 3), 24, np.uint8)
        cv2.line(image, (0, self.height - 1), (width, self.height - 1), (70, 70, 70), 1)
        cv2.putText(image, "Cam", (8, 21), cv2.FONT_HERSHEY_SIMPLEX, 0.4, (190, 190, 190), 1, cv2.LINE_AA)
        hits = self.layout(width)
        status_x = 8
        for hit in hits:
            if hit.kind == "button":
                self._draw_button(image, hit, session)
                status_x = max(status_x, hit.rect[0] + hit.rect[2] + 8)
            elif hit.kind == "check":
                self._draw_check(image, hit, session)
                status_x = max(status_x, hit.rect[0] + hit.rect[2] + 8)
            elif hit.kind == "field":
                self._draw_field(image, hit, session)
                status_x = max(status_x, hit.rect[0] + hit.rect[2] + 8)
            elif hit.kind == "fader" and hit.track is not None:
                self._draw_fader(image, hit, session)
        host_hit = next(hit for hit in hits if hit.key == "host")
        cv2.putText(
            image,
            "OSC",
            (host_hit.rect[0] - 32, 21),
            cv2.FONT_HERSHEY_SIMPLEX,
            0.4,
            (190, 190, 190),
            1,
            cv2.LINE_AA,
        )
        if session.status and status_x < width - 40:
            self._fit_text(image, session.status, status_x, 21, width - 8, (168, 168, 168))
        return image

    def press(self, x: int, y: int, width: int, session: Session) -> str | None:
        hit = self._hit_at(x, y, width)
        if hit is None:
            self.focus = None
            self._drag_key = None
            return None
        if hit.kind == "fader":
            self.focus = None
            self._drag_key = hit.key
            self._apply_fader(session, hit, x)
            return "fader"
        self._drag_key = None
        if hit.key == "camera-prev":
            session.camera_index = max(0, session.camera_index - 1)
            return "camera"
        if hit.key == "camera-next":
            session.camera_index = min(8, session.camera_index + 1)
            return "camera"
        if hit.key == "pick":
            session.pick = not session.pick
            return "pick"
        if hit.key == "debug":
            session.debug = not session.debug
            return "debug"
        if hit.key == "hands":
            session.hands = not session.hands
            return "hands"
        if hit.key == "save":
            return "save"
        if hit.key == "autofocus":
            session.autofocus = not session.autofocus
            return "autofocus"
        if hit.key in ("host", "port"):
            self.focus = hit.key
            self._edit = session.host if hit.key == "host" else str(session.port)
            return "focus"
        return None

    def drag(self, x: int, y: int, width: int, session: Session) -> None:
        del y
        if self._drag_key is None:
            return
        for hit in self.layout(width):
            if hit.kind == "fader" and hit.key == self._drag_key:
                self._apply_fader(session, hit, x)
                return

    def end_drag(self) -> None:
        self._drag_key = None

    def key(self, code: int, session: Session) -> str | None:
        """Handle a key while a text field is focused. None means the key is free."""

        if self.focus is None:
            return None
        if code in (13, 10):
            self._commit(session)
            self.focus = None
            return "osc"
        if code == 27:
            self.focus = None
            return "typing"
        if code in (8, 127):
            self._edit = self._edit[:-1]
            return "typing"
        if self.focus == "host" and 32 <= code < 127 and len(self._edit) < 64:
            self._edit += chr(code)
        elif self.focus == "port" and 48 <= code <= 57 and len(self._edit) < 5:
            self._edit += chr(code)
        return "typing"

    def _commit(self, session: Session) -> None:
        if self.focus == "host":
            host = self._edit.strip()
            if host:
                session.host = host
        elif self.focus == "port":
            try:
                port = int(self._edit)
            except ValueError:
                return
            if 1 <= port <= 65535:
                session.port = port

    def _hit_at(self, x: int, y: int, width: int) -> _Hit | None:
        for hit in reversed(self.layout(width)):
            left, top, wid, hei = hit.rect
            if left <= x <= left + wid and top <= y <= top + hei:
                return hit
        return None

    def _apply_fader(self, session: Session, hit: _Hit, x: int) -> None:
        spec = _FADER_BY_KEY[hit.key]
        assert hit.track is not None
        track_x, _track_y, track_w, _track_h = hit.track
        amount = min(1.0, max(0.0, (x - track_x) / max(track_w - 1, 1)))
        value = spec.low + amount * (spec.high - spec.low)
        stored: int | float
        if spec.decimals == 0:
            stored = int(round(value))
        else:
            stored = round(value, spec.decimals)
        session.calibration = replace(session.calibration, **{spec.key: stored}).clamped()

    def _draw_button(self, image: np.ndarray, hit: _Hit, session: Session) -> None:
        left, top, wid, hei = hit.rect
        active = (
            (hit.key == "pick" and session.pick)
            or (hit.key == "debug" and session.debug)
            or (hit.key == "hands" and session.hands)
        )
        color = (70, 150, 90) if active else (48, 48, 48)
        cv2.rectangle(image, (left, top), (left + wid, top + hei), color, -1)
        cv2.rectangle(image, (left, top), (left + wid, top + hei), (120, 120, 120), 1)
        text = {
            "camera-prev": "<",
            "camera-next": ">",
            "pick": "Fijar",
            "debug": "Mascara",
            "hands": "Manos",
            "save": "Guardar",
        }[hit.key]
        if hit.key == "camera-prev":
            index = str(session.camera_index)
            cv2.putText(
                image, index, (left + 26, top + 16), cv2.FONT_HERSHEY_SIMPLEX, 0.45, (240, 240, 240), 1, cv2.LINE_AA
            )
        size = cv2.getTextSize(text, cv2.FONT_HERSHEY_SIMPLEX, 0.4, 1)[0]
        cv2.putText(
            image,
            text,
            (left + max(2, (wid - size[0]) // 2), top + 16),
            cv2.FONT_HERSHEY_SIMPLEX,
            0.4,
            (245, 245, 245),
            1,
            cv2.LINE_AA,
        )

    def _draw_check(self, image: np.ndarray, hit: _Hit, session: Session) -> None:
        left, top, _wid, hei = hit.rect
        box = 14
        by = top + (hei - box) // 2
        fill = (70, 150, 90) if session.autofocus else (32, 32, 32)
        cv2.rectangle(image, (left, by), (left + box, by + box), fill, -1)
        cv2.rectangle(image, (left, by), (left + box, by + box), (160, 160, 160), 1)
        if session.autofocus:
            cv2.line(image, (left + 3, by + 7), (left + 6, by + 11), (245, 245, 245), 1, cv2.LINE_AA)
            cv2.line(image, (left + 6, by + 11), (left + 11, by + 3), (245, 245, 245), 1, cv2.LINE_AA)
        cv2.putText(
            image,
            "Foco",
            (left + box + 6, top + 16),
            cv2.FONT_HERSHEY_SIMPLEX,
            0.4,
            (235, 235, 235),
            1,
            cv2.LINE_AA,
        )

    def _draw_field(self, image: np.ndarray, hit: _Hit, session: Session) -> None:
        left, top, wid, hei = hit.rect
        focused = self.focus == hit.key
        cv2.rectangle(image, (left, top), (left + wid, top + hei), (18, 18, 18), -1)
        border = (120, 210, 160) if focused else (90, 90, 90)
        cv2.rectangle(image, (left, top), (left + wid, top + hei), border, 1)
        if focused:
            text = self._edit + "|"
        elif hit.key == "host":
            text = session.host
        else:
            text = str(session.port)
        self._fit_text(image, text, left + 4, top + 16, left + wid - 4, (235, 235, 235))

    def _draw_fader(self, image: np.ndarray, hit: _Hit, session: Session) -> None:
        spec = _FADER_BY_KEY[hit.key]
        assert hit.track is not None
        value = float(getattr(session.calibration, spec.key))
        amount = (value - spec.low) / max(1e-6, spec.high - spec.low)
        amount = min(1.0, max(0.0, amount))
        track_x, track_y, track_w, track_h = hit.track
        cv2.rectangle(image, (track_x, track_y), (track_x + track_w, track_y + track_h), (52, 52, 52), -1)
        fill = track_x + int(round(track_w * amount))
        cv2.rectangle(image, (track_x, track_y), (fill, track_y + track_h), (80, 170, 200), -1)
        cv2.line(image, (fill, track_y - 2), (fill, track_y + track_h + 2), (245, 245, 245), 1)
        shown = f"{value:.{spec.decimals}f}" if spec.decimals else _compact(value)
        self._fit_text(image, spec.label, hit.rect[0], hit.rect[1] + 15, track_x - 3, (220, 220, 220))
        value_w = cv2.getTextSize(shown, cv2.FONT_HERSHEY_SIMPLEX, 0.38, 1)[0][0]
        value_x = hit.rect[0] + hit.rect[2] - value_w
        cv2.putText(
            image,
            shown,
            (value_x, hit.rect[1] + 15),
            cv2.FONT_HERSHEY_SIMPLEX,
            0.38,
            (235, 235, 235),
            1,
            cv2.LINE_AA,
        )

    def _fit_text(
        self,
        image: np.ndarray,
        text: str,
        x: int,
        baseline: int,
        limit: int,
        color: tuple[int, int, int],
    ) -> None:
        font = cv2.FONT_HERSHEY_SIMPLEX
        shown = text
        while shown and cv2.getTextSize(shown, font, 0.38, 1)[0][0] > max(8, limit - x):
            shown = shown[:-1]
        if not shown:
            return
        cv2.putText(image, shown, (x, baseline), font, 0.38, color, 1, cv2.LINE_AA)


def _compact(value: float) -> str:
    number = int(round(value))
    if abs(number) >= 1000:
        return f"{number / 1000:.0f}k" if number % 1000 == 0 else f"{number / 1000:.1f}k"
    return str(number)


def _check_bar() -> None:
    bar = TopBar()
    session = Session(calibration=Calibration())
    widgets = {hit.key: hit for hit in bar.layout(900)}
    track = widgets["exposure"].track
    assert track is not None
    bar.press(track[0] + track[2] - 1, track[1] + 2, 900, session)
    if session.calibration.exposure != 100:
        raise RuntimeError(f"exposure fader landed on {session.calibration.exposure}")
    bar.press(track[0], track[1] + 2, 900, session)
    if session.calibration.exposure != 0:
        raise RuntimeError(f"exposure fader landed on {session.calibration.exposure}")
    bar.end_drag()
    pick = widgets["pick"].rect
    bar.press(pick[0] + 2, pick[1] + 2, 900, session)
    if not session.pick:
        raise RuntimeError("Fijar did not toggle")
    focus = widgets["autofocus"].rect
    bar.press(focus[0] + 4, focus[1] + 4, 900, session)
    if not session.autofocus:
        raise RuntimeError("Foco did not toggle")
    host = widgets["host"].rect
    bar.press(host[0] + 4, host[1] + 4, 900, session)
    for _ in range(4):
        if bar.key(127, session) != "typing":
            raise RuntimeError("host field did not take backspace")
    if bar.key(13, session) != "osc":
        raise RuntimeError("enter did not commit the host")
    if session.host != "127.0":
        raise RuntimeError(f"host commit produced {session.host}")
    image = bar.draw(900, session)
    if image.shape[0] != TopBar.height or image.shape[1] != 900:
        raise RuntimeError("top bar has the wrong size")
    print("top bar self-test passed")


class CameraControl:
    """Push exposure and gain to the camera, or report that the driver ignores them."""

    def __init__(self, cap: cv2.VideoCapture, force_software: bool = False):
        self.cap = cap
        self.support = HardwareSupport()
        self._exposure_dark = -13.0
        self._exposure_bright = -1.0
        self._gain_prop = cv2.CAP_PROP_GAIN
        self._gain_dark = 0.0
        self._gain_bright = 100.0
        self._gain_kind = "gain"
        self._last_exposure: float | None = None
        self._last_gain: float | None = None
        self._last_key: tuple[int, int] | None = None
        self._read_failed = False
        self._autofocus: bool | None = None
        self._disable_auto()
        if force_software:
            print("Camera exposure and gain forced to software.")
            return
        print("Probing camera exposure and gain...")
        try:
            self._probe()
        except Exception as exc:
            print(f"Camera control probe failed ({exc}). Using software exposure and gain.")
            self.support = HardwareSupport()

    def apply(self, cal: Calibration) -> None:
        cal = cal.clamped()
        key = (cal.exposure, cal.gain)
        if key == self._last_key:
            return
        self._last_key = key
        if self.support.exposure:
            self._disable_auto()
            value = _lerp(cal.exposure, self._exposure_dark, self._exposure_bright)
            self.cap.set(cv2.CAP_PROP_EXPOSURE, value)
            self._last_exposure = value
        if self.support.gain:
            value = _lerp(cal.gain, self._gain_dark, self._gain_bright)
            self.cap.set(self._gain_prop, value)
            self._last_gain = value

    def set_autofocus(self, enabled: bool) -> None:
        """Keep the lens from hunting. The checkbox starts off."""

        self.cap.set(cv2.CAP_PROP_AUTOFOCUS, 1.0 if enabled else 0.0)
        if enabled or self._autofocus is False:
            self._autofocus = enabled
            return
        self._autofocus = False
        focus = float(self.cap.get(cv2.CAP_PROP_FOCUS))
        if focus > 0.0:
            self.cap.set(cv2.CAP_PROP_FOCUS, focus)

    def status_line(self) -> str:
        if self.support.exposure and self._last_exposure is not None:
            exposure = f"expo HW {self._last_exposure:.1f}"
        elif self.support.exposure:
            exposure = "expo HW"
        else:
            exposure = "expo SW"
        if self.support.gain and self._last_gain is not None:
            gain = f"{'gan' if self._gain_kind == 'gain' else self._gain_kind} HW {self._last_gain:.0f}"
        elif self.support.gain:
            gain = f"{'gan' if self._gain_kind == 'gain' else self._gain_kind} HW"
        else:
            gain = "gan SW"
        return f"{exposure}   {gain}"

    def _disable_auto(self) -> None:
        # Backends disagree on the manual-exposure flag. Setting the usual
        # candidates is what makes a later CAP_PROP_EXPOSURE write stick.
        for value in (0.25, 1.0, 0.0):
            self.cap.set(cv2.CAP_PROP_AUTO_EXPOSURE, value)

    def _probe(self) -> None:
        self.support.exposure = self._probe_exposure()
        if self._read_failed:
            return
        if self.support.exposure:
            middle = (self._exposure_dark + self._exposure_bright) / 2.0
            self.cap.set(cv2.CAP_PROP_EXPOSURE, middle)
        self.support.gain = self._probe_gain()

    def _probe_exposure(self) -> bool:
        # (sample_dark, sample_bright, slider_dark, slider_bright)
        candidates = (
            (-11.0, -2.0, -13.0, -1.0),
            (30.0, 180.0, 0.0, 255.0),
            (100.0, 800.0, 50.0, 1600.0),
        )
        for sample_a, sample_b, span_a, span_b in candidates:
            mean_a = self._mean_after(cv2.CAP_PROP_EXPOSURE, sample_a)
            mean_b = self._mean_after(cv2.CAP_PROP_EXPOSURE, sample_b)
            if self._read_failed or mean_a is None or mean_b is None:
                return False
            if abs(mean_b - mean_a) < 5.0:
                continue
            if mean_a <= mean_b:
                self._exposure_dark, self._exposure_bright = span_a, span_b
            else:
                self._exposure_dark, self._exposure_bright = span_b, span_a
            return True
        return False

    def _probe_gain(self) -> bool:
        candidates = (
            (cv2.CAP_PROP_GAIN, 0.0, 48.0, 0.0, 100.0, "gain"),
            (cv2.CAP_PROP_ISO_SPEED, 100.0, 800.0, 100.0, 1600.0, "ISO"),
        )
        for prop, sample_a, sample_b, span_a, span_b, kind in candidates:
            if not self._readback_moves(prop, sample_a, sample_b):
                continue
            mean_a = self._mean_after(prop, sample_a)
            mean_b = self._mean_after(prop, sample_b)
            if self._read_failed or mean_a is None or mean_b is None:
                return False
            if abs(mean_b - mean_a) < 3.0:
                continue
            self._gain_prop = prop
            self._gain_kind = kind
            if mean_a <= mean_b:
                self._gain_dark, self._gain_bright = span_a, span_b
            else:
                self._gain_dark, self._gain_bright = span_b, span_a
            return True
        return False

    def _mean_after(self, prop: int, value: float) -> float | None:
        self.cap.set(prop, float(value))
        frame = None
        for _ in range(2):
            ok, grabbed = self.cap.read()
            if ok and grabbed is not None and grabbed.size:
                frame = grabbed
        if frame is None:
            self._read_failed = True
            return None
        return float(frame.mean())

    def _readback_moves(self, prop: int, first: float, second: float) -> bool:
        wrote_first = bool(self.cap.set(prop, float(first)))
        read_first = float(self.cap.get(prop))
        wrote_second = bool(self.cap.set(prop, float(second)))
        read_second = float(self.cap.get(prop))
        if not wrote_first and not wrote_second:
            return False
        return abs(read_second - read_first) > 0.5


def image_adjustments(
    cal: Calibration, support: HardwareSupport
) -> tuple[float, float, float]:
    """Contrast, brightness, and gamma, plus a software stand-in for locked controls.

    Exposure 50 and gain 0 are neutral. When the driver ignores the hardware
    property, the same sliders shift beta and gamma instead.
    """

    cal = cal.clamped()
    alpha = cal.contrast
    beta = float(cal.brightness)
    gamma = cal.gamma
    if not support.exposure:
        # The LUT below treats gamma > 1 as a midtone lift (brighter).
        tilt = (cal.exposure - 50) / 50.0
        beta += tilt * 40.0
        if tilt >= 0.0:
            gamma = gamma * (1.0 + 0.55 * tilt)
        else:
            gamma = gamma / (1.0 - 0.45 * tilt)
    if not support.gain:
        amount = cal.gain / 100.0
        alpha *= 1.0 + 0.55 * amount
        beta += 20.0 * amount
    alpha = float(np.clip(alpha, 0.2, 6.0))
    beta = float(np.clip(beta, -150.0, 150.0))
    gamma = float(np.clip(gamma, 0.20, 3.5))
    return alpha, beta, gamma


def apply_image_adjustments(
    frame: np.ndarray, cal: Calibration, support: HardwareSupport
) -> np.ndarray:
    alpha, beta, gamma = image_adjustments(cal, support)
    adjusted = frame
    if abs(alpha - 1.0) > 1e-3 or abs(beta) > 1e-3:
        adjusted = cv2.convertScaleAbs(frame, alpha=alpha, beta=beta)
    if abs(gamma - 1.0) > 0.02:
        adjusted = _apply_gamma(adjusted, gamma)
    return adjusted


_GAMMA_LUTS: dict[int, np.ndarray] = {}


def _apply_gamma(frame: np.ndarray, gamma: float) -> np.ndarray:
    key = int(round(gamma * 100))
    table = _GAMMA_LUTS.get(key)
    if table is None:
        inv = 1.0 / max(gamma, 0.05)
        table = np.array(
            [min(255, int(round((i / 255.0) ** inv * 255.0))) for i in range(256)],
            dtype=np.uint8,
        )
        _GAMMA_LUTS[key] = table
    return cv2.LUT(frame, table)


def _lerp(slider: int, dark: float, bright: float) -> float:
    return dark + (bright - dark) * (int(slider) / 100.0)


def _as_int(value: object, fallback: int, low: int, high: int) -> int:
    return int(np.clip(round(_as_float(value, float(fallback))), low, high))


def _as_float(value: object, fallback: float) -> float:
    try:
        number = float(value)  # type: ignore[arg-type]
    except (TypeError, ValueError) as exc:
        raise ValueError(f"expected a number, got {value!r}") from exc
    if not np.isfinite(number):
        raise ValueError(f"expected a finite number, got {value!r}")
    return number


def run_self_test() -> None:
    import tempfile

    custom = Calibration(
        exposure=20,
        gain=40,
        contrast=1.8,
        brightness=-15,
        gamma=1.4,
        min_brightness=210,
        max_saturation=30,
        min_blob_area=2500,
        max_blob_area=120000,
        max_radial_zone=500,
    )
    restored = calibration_from_positions(positions_from_calibration(custom))
    _check(restored == custom.clamped(), f"slider roundtrip changed values: {restored}")

    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory) / "nested" / "calibration.json"
        save_calibration(path, custom)
        loaded, message = load_calibration(path)
        _check("Calibracion cargada" in message, message)
        _check(loaded == custom.clamped(), f"JSON roundtrip changed values: {loaded}")

        broken = Path(directory) / "broken.json"
        broken.write_text("{", encoding="utf-8")
        fallback, broken_message = load_calibration(broken)
        _check(fallback == Calibration(), "broken JSON should fall back to defaults")
        _check("No se pudo leer" in broken_message, broken_message)

        partial = Path(directory) / "partial.json"
        partial.write_text('{"_note": "x", "exposure": 10, "extra": 1}\n', encoding="utf-8")
        merged, _ = load_calibration(partial)
        _check(merged.exposure == 10, f"partial file exposure was {merged.exposure}")
        _check(merged.contrast == 1.0, "missing keys should keep defaults")

    support = HardwareSupport(exposure=False, gain=False)
    gray = np.full((24, 24, 3), 100, np.uint8)
    brighter = apply_image_adjustments(gray, Calibration(exposure=100, gain=0), support)
    darker = apply_image_adjustments(gray, Calibration(exposure=0, gain=0), support)
    _check(float(brighter.mean()) > float(gray.mean()) + 10, "software exposure should brighten")
    _check(float(darker.mean()) < float(gray.mean()) - 10, "software exposure should darken")
    neutral = apply_image_adjustments(gray, Calibration(), support)
    _check(np.array_equal(neutral, gray), "default sliders should leave the frame unchanged")
    _check_bar()
    print("calibration self-test passed")


def _check(condition: bool, detail: str) -> None:
    if not condition:
        raise RuntimeError(detail)


if __name__ == "__main__":
    run_self_test()
