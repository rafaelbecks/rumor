#!/usr/bin/env python3
"""Real-time white-anchor tracker. Streams radial proximity over OSC.

Run `python main.py --help` for the sliders, keys, and OSC addresses.
"""

from __future__ import annotations

import argparse
import sys
import time
from pathlib import Path

if sys.version_info < (3, 10):
    sys.exit(
        "Python 3.10 or newer is required. "
        f"This interpreter is {sys.version.split()[0]}."
    )

import cv2
import numpy as np

from calibration import (
    WINDOW_NAME,
    Calibration,
    CameraControl,
    HardwareSupport,
    Session,
    TopBar,
    apply_image_adjustments,
    load_calibration,
    save_calibration,
)
from calibration import run_self_test as run_calibration_self_test
from osc_sender import OscSender
from osc_sender import run_self_test as run_osc_self_test
from tracker import (
    FIXTURE_CALIBRATION,
    DistanceLerp,
    RadialTracker,
    TrackResult,
    make_fixture_frame,
)
from tracker import run_self_test as run_tracker_self_test

INSTRUCTIONS = """
Vision soterrada — ancla blanca o translucida y proximidad radial

Desde el directorio vision, con el entorno virtual activo:

  python main.py
  python main.py --camera 0 --host 127.0.0.1 --port 8000
  python main.py --width 1280 --height 720 --calibration calibration.json
  python main.py --hands --debug --software-exposure
  python main.py --headless --frames 150 --width 640 --height 480 --no-osc
  python main.py --self-test

--headless no abre ventana. Escribe los fps en la terminal una vez por segundo.
--frames N corta despues de N cuadros e imprime el promedio. Ctrl-C tambien corta.

La barra tiene la camara, el host y puerto OSC, y los faders.
Clic en el host o el puerto, escribir, y Enter para aplicar.
Fijar dibuja un cuadro en cada blob. Un clic fija ese objeto y guarda
el cuadro actual como referencia vacia. El objeto fijado no se mueve
a otro blob. Clic en el fondo para soltarlo.
Foco empieza apagado. Guardar escribe los faders, no el objeto.

Teclas
  P       modo fijar
  D       mascara, anillos y distancias
  S       guardar los faders
  L       recargar ese archivo
  H       manos MediaPipe
  Q, ESC  salir

Faders
  Expo y Gan usan la camara cuando el driver los acepta.
  Si la barra dice SW, esos faders se aplican en software.
  Cont es 1.0–3.0, Brillo es -100–100, Gamma es 0.30–3.00.
  Min V es el valor HSV y la luminosidad LAB. Max S rechaza el color.
  A min y A max son pixeles. Zona es el perimetro alrededor del objeto.
  Suave es el lerp de la distancia: 0 sigue al instante, 100 la frena.

OSC, un paquete por cuadro
  /anchor/position   norm_x  norm_y
  /radial/nearest    normalized_distance  raw_pixel_distance  angle_degrees
  /radial/distance   normalized_distance
  /radial/frame      frame_distance
  /radial/objects    count  dist_1  dist_2 ...
  Sin ancla: -1 -1. Zona vacia: -1 -1 -1, y count 0.
""".strip()

_DISPLAY_MAX = (1440, 900)


def main(argv: list[str] | None = None) -> int:
    args = _parse_args(argv)
    if args.self_test:
        _run_self_tests()
        return 0

    print(INSTRUCTIONS)
    print()
    calibration_path = Path(args.calibration)
    calibration, loaded = load_calibration(calibration_path)
    print(loaded)
    print(
        f"Camera {args.camera}   OSC "
        f"{'off' if args.no_osc else f'{args.host}:{args.port}'}   "
        f"calibration {calibration_path}"
    )

    capture = _open_camera(args.camera, args.width, args.height)
    session = Session(
        calibration=calibration,
        camera_index=args.camera,
        host=args.host,
        port=args.port,
        debug=bool(args.debug),
        hands=bool(args.hands),
    )
    bar = TopBar()
    camera = CameraControl(capture, force_software=args.software_exposure)
    print(camera.status_line())
    tracker = RadialTracker()
    smoother = DistanceLerp()
    sender = None if args.no_osc else OscSender(session.host, session.port)
    if session.hands:
        tracker.set_hands(True)
    open_index = session.camera_index
    headless = bool(args.headless)
    frame_limit = max(0, int(args.frames))
    if headless:
        print("Headless. FPS is printed here once a second. Ctrl-C stops.")
    else:
        cv2.namedWindow(WINDOW_NAME, cv2.WINDOW_NORMAL)
    ctx: dict[str, object] = {
        "session": session,
        "bar": bar,
        "tracker": tracker,
        "shown": None,
        "frame_wh": (args.width, args.height),
        "video_wh": (args.width, args.height),
        "calibration_path": calibration_path,
    }
    if not headless:
        cv2.setMouseCallback(WINDOW_NAME, lambda event, x, y, flags, _param: _on_mouse(event, x, y, flags, ctx))

    fps = 0.0
    previous = time.perf_counter()
    logged_at = previous
    started = previous
    counted = 0
    misses = 0
    announced = False
    window_was_visible = False
    try:
        while True:
            if session.camera_index != open_index:
                capture, camera, open_index = _switch_camera(
                    capture, camera, session, open_index, args.width, args.height, args.software_exposure
                )
            if session.hands != tracker.hands_enabled:
                tracker.set_hands(session.hands)
            if sender is not None and (sender.host, sender.port) != (session.host, session.port):
                sender = OscSender(session.host, session.port)
                print(f"OSC {session.host}:{session.port}")

            camera.set_autofocus(session.autofocus)
            camera.apply(session.calibration)
            ok, frame = capture.read()
            if not ok or frame is None or frame.size == 0:
                misses += 1
                if misses > 40:
                    print("The camera stopped producing frames.")
                    break
                continue
            misses = 0
            if not announced:
                height, width = frame.shape[:2]
                print(f"Camera frame {width}x{height}")
                announced = True
                started = time.perf_counter()

            adjusted = apply_image_adjustments(frame, session.calibration, camera.support)
            ctx["adjusted"] = adjusted
            result = tracker.process(adjusted, session.calibration)
            result = smoother.apply(
                result,
                session.calibration.smoothing / 100.0,
                float(session.calibration.max_radial_zone),
            )
            if sender is not None:
                sender.send_result(result)

            now = time.perf_counter()
            delta = now - previous
            previous = now
            if delta > 0:
                sample = 1.0 / delta
                fps = sample if fps == 0.0 else (0.9 * fps + 0.1 * sample)
            counted += 1
            session.status = _status_line(result, session, camera, fps)
            if headless and now - logged_at >= 1.0:
                print(session.status, flush=True)
                logged_at = now
            if frame_limit and counted >= frame_limit:
                break
            if headless:
                continue

            video = render_view(adjusted, result, session.calibration, session.debug, session.pick)
            shown = np.vstack([bar.draw(video.shape[1], session), video])
            ctx["shown"] = shown
            ctx["frame_wh"] = (frame.shape[1], frame.shape[0])
            ctx["video_wh"] = (video.shape[1], video.shape[0])
            cv2.imshow(WINDOW_NAME, shown)
            visible = _window_visibility()
            if visible is not None and visible >= 1:
                window_was_visible = True
            elif window_was_visible:
                break

            raw_key = cv2.waitKey(1)
            if raw_key == -1:
                continue
            key = raw_key & 0xFF
            if bar.key(key, session) is not None:
                continue
            if key in (ord("q"), ord("Q"), 27):
                break
            if key in (ord("d"), ord("D")):
                session.debug = not session.debug
                print("mascara on" if session.debug else "mascara off")
            elif key in (ord("p"), ord("P")):
                session.pick = not session.pick
                print("fijar on — clic en un cuadro" if session.pick else "fijar off")
            elif key in (ord("s"), ord("S")):
                save_calibration(calibration_path, session.calibration)
                print(f"Calibracion guardada en {calibration_path}")
            elif key in (ord("l"), ord("L")):
                session.calibration, message = load_calibration(calibration_path)
                print(message)
            elif key in (ord("h"), ord("H")):
                session.hands = not session.hands
    except KeyboardInterrupt:
        print("\nStopped.")
    finally:
        if headless and counted > 0:
            elapsed = max(1e-6, time.perf_counter() - started)
            print(f"average {counted / elapsed:.1f} fps over {counted} frames")
        tracker.close()
        capture.release()
        if not headless:
            cv2.destroyAllWindows()
    return 0


def render_view(
    frame: np.ndarray,
    result: TrackResult,
    calibration: Calibration,
    debug: bool,
    pick: bool,
) -> np.ndarray:
    view = frame.copy()
    _draw_tracking(view, result, calibration, debug)
    _draw_squares(view, result, pick)
    if debug:
        mask_view = _mask_panel(result, calibration)
        height, width = view.shape[:2]
        left_w = max(1, width // 2)
        right_w = max(1, width - left_w)
        left = cv2.resize(view, (left_w, height), interpolation=cv2.INTER_AREA)
        right = cv2.resize(mask_view, (right_w, height), interpolation=cv2.INTER_AREA)
        _outlined(left, "imagen", (8, height - 12), scale=0.45)
        _outlined(right, "mascara", (8, height - 12), scale=0.45)
        view = np.hstack([left, right])
    return _fit_display(view)


def _parse_args(argv: list[str] | None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Track a white/translucent anchor and broadcast radial proximity over OSC.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=INSTRUCTIONS,
    )
    parser.add_argument("--camera", type=int, default=0, help="camera index (default 0)")
    parser.add_argument("--width", type=int, default=1280, help="requested capture width")
    parser.add_argument("--height", type=int, default=720, help="requested capture height")
    parser.add_argument("--host", default="127.0.0.1", help="OSC destination (default 127.0.0.1)")
    parser.add_argument("--port", type=int, default=8000, help="OSC UDP port (default 8000)")
    parser.add_argument(
        "--calibration",
        default=str(Path(__file__).resolve().with_name("calibration.json")),
        help="path to calibration.json",
    )
    parser.add_argument("--hands", action="store_true", help="start with MediaPipe hands enabled")
    parser.add_argument("--debug", action="store_true", help="start in the debug view")
    parser.add_argument(
        "--software-exposure",
        action="store_true",
        help="ignore camera exposure and gain even if the driver accepts them",
    )
    parser.add_argument("--no-osc", action="store_true", help="run the view without sending OSC")
    parser.add_argument(
        "--headless",
        action="store_true",
        help="no window; print fps to the terminal once a second",
    )
    parser.add_argument(
        "--frames",
        type=int,
        default=0,
        help="stop after this many frames and print the average fps (0 runs until quit)",
    )
    parser.add_argument(
        "--self-test",
        action="store_true",
        help="check the tracker, calibration, and OSC on a synthetic scene, then exit",
    )
    return parser.parse_args(argv)


def _open_camera(index: int, width: int, height: int) -> cv2.VideoCapture:
    capture = _try_open_camera(index, width, height)
    if capture is None:
        sys.exit(
            f"Could not open camera {index}. Allow camera access for this terminal "
            f"or Cursor, confirm the camera is connected, or try --camera 1."
        )
    return capture


def _try_open_camera(index: int, width: int, height: int) -> cv2.VideoCapture | None:
    backends: list[int] = []
    if sys.platform == "darwin":
        backends.append(cv2.CAP_AVFOUNDATION)
    elif sys.platform.startswith("linux"):
        backends.append(cv2.CAP_V4L2)
    backends.append(cv2.CAP_ANY)
    for backend in backends:
        capture = cv2.VideoCapture(index, backend)
        if not capture.isOpened():
            capture.release()
            continue
        capture.set(cv2.CAP_PROP_FRAME_WIDTH, width)
        capture.set(cv2.CAP_PROP_FRAME_HEIGHT, height)
        capture.set(cv2.CAP_PROP_FPS, 30)
        capture.set(cv2.CAP_PROP_BUFFERSIZE, 1)
        return capture
    return None


def _switch_camera(
    capture: cv2.VideoCapture,
    camera: CameraControl,
    session: Session,
    open_index: int,
    width: int,
    height: int,
    force_software: bool,
) -> tuple[cv2.VideoCapture, CameraControl, int]:
    opened = _try_open_camera(session.camera_index, width, height)
    if opened is None:
        print(f"Camera {session.camera_index} did not open.")
        session.camera_index = open_index
        return capture, camera, open_index
    capture.release()
    control = CameraControl(opened, force_software=force_software)
    print(f"Camera {session.camera_index}")
    print(control.status_line())
    return opened, control, session.camera_index


def _draw_tracking(
    view: np.ndarray, result: TrackResult, calibration: Calibration, debug: bool
) -> None:
    anchor = result.anchor
    if anchor is None:
        return
    center = (int(round(anchor.x)), int(round(anchor.y)))
    zone = int(round(calibration.max_radial_zone))
    if zone > 1:
        cv2.circle(view, center, zone, (230, 210, 70), 2, cv2.LINE_AA)
    if result.locked:
        object_radius = max(1, int(round(anchor.radius)))
        cv2.circle(view, center, object_radius, (80, 230, 230), 2, cv2.LINE_AA)
        if (
            result.smooth_raw is not None
            and result.smooth_norm is not None
            and result.smooth_raw >= 0.0
            and result.smooth_norm >= 0.0
        ):
            measure = max(object_radius, int(round(anchor.radius + result.smooth_raw)))
            cv2.circle(view, center, measure, (40, 170, 255), 2, cv2.LINE_AA)
            label_at = (min(view.shape[1] - 48, center[0] + measure + 8), max(18, center[1]))
            _outlined(view, f"{result.smooth_norm:.2f}", label_at, scale=0.5, color=(40, 170, 255))
    if debug and zone > anchor.radius + 4:
        for fraction in (0.25, 0.5, 0.75):
            ring = anchor.radius + (zone - anchor.radius) * fraction
            cv2.circle(view, center, int(round(ring)), (150, 130, 40), 1, cv2.LINE_AA)
    cv2.drawContours(view, [anchor.contour], -1, (90, 220, 120), 2, cv2.LINE_AA)
    cv2.circle(view, center, 4, (245, 245, 245), -1, cv2.LINE_AA)
    if not debug:
        return
    height, width = view.shape[:2]
    for neighbor in reversed(result.neighbors):
        color = _heat(neighbor.normalized)
        if neighbor.contour is not None:
            cv2.drawContours(view, [neighbor.contour], -1, color, 1, cv2.LINE_AA)
        for px, py in neighbor.points:
            cv2.circle(view, (int(round(px)), int(round(py))), 2, color, -1, cv2.LINE_AA)
        start = anchor.edge_toward(neighbor.nearest_x, neighbor.nearest_y)
        end = (int(round(neighbor.nearest_x)), int(round(neighbor.nearest_y)))
        cv2.line(
            view,
            (int(round(start[0])), int(round(start[1]))),
            end,
            color,
            2,
            cv2.LINE_AA,
        )
        cv2.circle(view, end, 3, color, -1, cv2.LINE_AA)
        label = f"{neighbor.normalized:.2f}  {neighbor.raw_distance:.0f}px"
        if neighbor.source == "hand":
            label += "  hand"
        dx = neighbor.nearest_x - start[0]
        dy = neighbor.nearest_y - start[1]
        length = (dx * dx + dy * dy) ** 0.5 or 1.0
        origin = (
            int(round((start[0] + neighbor.nearest_x) / 2.0 - dy / length * 14)),
            int(round((start[1] + neighbor.nearest_y) / 2.0 + dx / length * 14)),
        )
        origin = (min(max(origin[0], 4), width - 4), min(max(origin[1], 16), height - 4))
        _outlined(view, label, origin, scale=0.45, color=color)


def _draw_squares(view: np.ndarray, result: TrackResult, pick: bool) -> None:
    if pick:
        for candidate in result.candidates:
            _square(view, candidate.bbox, (210, 210, 210), 1)
    if result.locked and result.anchor is not None:
        _square(view, cv2.boundingRect(result.anchor.contour), (80, 230, 120), 2)


def _square(view: np.ndarray, bbox: tuple[int, int, int, int], color: tuple[int, int, int], thickness: int) -> None:
    x, y, width, height = bbox
    pad = 4
    cv2.rectangle(
        view,
        (x - pad, y - pad),
        (x + width + pad, y + height + pad),
        color,
        thickness,
        cv2.LINE_AA,
    )


def _mask_panel(result: TrackResult, calibration: Calibration) -> np.ndarray:
    panel = cv2.cvtColor(result.white_mask, cv2.COLOR_GRAY2BGR)
    anchor = result.anchor
    if anchor is None:
        return panel
    center = (int(round(anchor.x)), int(round(anchor.y)))
    zone = int(round(calibration.max_radial_zone))
    if zone > 1:
        cv2.circle(panel, center, zone, (230, 210, 70), 1, cv2.LINE_AA)
    cv2.drawContours(panel, [anchor.contour], -1, (90, 220, 120), 2, cv2.LINE_AA)
    return panel


def _status_line(result: TrackResult, session: Session, camera: CameraControl, fps: float) -> str:
    if session.pick:
        held = "  referencia" if result.reference else ""
        return "clic en un cuadro para fijar — el fondo suelta" + held
    if result.anchor is None:
        place = "sin objeto"
    else:
        place = f"objeto {result.anchor.x:.0f},{result.anchor.y:.0f}"
        if result.neighbors:
            place += f"  cerca {result.neighbors[0].normalized:.2f}"
    lock = "referencia" if result.reference else "fijo" if result.locked else "auto"
    return f"{fps:.0f} fps  {camera.status_line()}  {place}  {lock}"


def _on_mouse(event: int, x: int, y: int, flags: int, ctx: dict[str, object]) -> None:
    shown = ctx["shown"]
    if not isinstance(shown, np.ndarray):
        return
    session = ctx["session"]
    bar = ctx["bar"]
    tracker = ctx["tracker"]
    if not isinstance(session, Session) or not isinstance(bar, TopBar) or not isinstance(tracker, RadialTracker):
        return
    ix, iy = _to_shown(x, y, shown.shape)
    width = shown.shape[1]
    if iy < 0 or ix < 0 or iy >= shown.shape[0] or ix >= width:
        return
    if event == cv2.EVENT_LBUTTONUP:
        bar.end_drag()
        return
    if event == cv2.EVENT_MOUSEMOVE and flags & cv2.EVENT_FLAG_LBUTTON:
        bar.drag(ix, iy, width, session)
        return
    if event != cv2.EVENT_LBUTTONDOWN:
        return
    if iy < bar.height:
        action = bar.press(ix, iy, width, session)
        if action == "save":
            path = ctx.get("calibration_path")
            if isinstance(path, Path):
                save_calibration(path, session.calibration)
                print(f"Calibracion guardada en {path}")
        return
    if not session.pick:
        return
    frame_wh = ctx["frame_wh"]
    video_wh = ctx["video_wh"]
    if not isinstance(frame_wh, tuple) or not isinstance(video_wh, tuple):
        return
    point = _video_to_frame(ix, iy - bar.height, video_wh, frame_wh, session.debug)
    if point is None:
        return
    if tracker.select_at(point[0], point[1]):
        adjusted = ctx.get("adjusted")
        if isinstance(adjusted, np.ndarray):
            tracker.capture_reference(adjusted)
        print(f"Objeto fijado en {point[0]:.0f}, {point[1]:.0f}. Referencia guardada.")
    else:
        print("Objeto suelto. Vuelve el blob blanco mas grande.")


def _video_to_frame(
    x: float,
    y: float,
    video_wh: tuple[int, int],
    frame_wh: tuple[int, int],
    debug: bool,
) -> tuple[float, float] | None:
    video_w, video_h = video_wh
    frame_w, frame_h = frame_wh
    if video_w <= 0 or video_h <= 0 or y < 0 or y > video_h:
        return None
    if debug:
        feed_w = video_w / 2.0
        if x < 0 or x >= feed_w:
            return None
        return x / feed_w * frame_w, y / video_h * frame_h
    if x < 0 or x > video_w:
        return None
    return x / video_w * frame_w, y / video_h * frame_h


def _to_shown(x: int, y: int, shape: tuple[int, ...]) -> tuple[int, int]:
    shown_h, shown_w = shape[:2]
    rect = None
    if sys.platform != "darwin":
        try:
            rect = cv2.getWindowImageRect(WINDOW_NAME)
        except cv2.error:
            rect = None
    # Cocoa already reports clicks in image pixels. getWindowImageRect there
    # is the window's position on the screen, and subtracting it lifts every
    # click off the bar (a fader at y=40 becomes y=-1400).
    return _map_window_point(x, y, shown_w, shown_h, rect, sys.platform == "darwin")


def _map_window_point(
    x: int,
    y: int,
    shown_w: int,
    shown_h: int,
    rect: tuple[int, int, int, int] | None,
    image_space: bool,
) -> tuple[int, int]:
    if image_space or not rect or rect[2] <= 0 or rect[3] <= 0:
        return int(x), int(y)
    rx, ry, rw, rh = rect
    return int(round((x - rx) * shown_w / rw)), int(round((y - ry) * shown_h / rh))


def _outlined(
    image: np.ndarray,
    text: str,
    origin: tuple[int, int],
    scale: float = 0.5,
    color: tuple[int, int, int] = (240, 240, 240),
) -> None:
    font = cv2.FONT_HERSHEY_SIMPLEX
    cv2.putText(image, text, origin, font, scale, (0, 0, 0), 3, cv2.LINE_AA)
    cv2.putText(image, text, origin, font, scale, color, 1, cv2.LINE_AA)


def _fit_display(image: np.ndarray) -> np.ndarray:
    height, width = image.shape[:2]
    scale = min(_DISPLAY_MAX[0] / width, _DISPLAY_MAX[1] / height, 1.0)
    if scale >= 0.999:
        return image
    return cv2.resize(
        image,
        (max(1, int(width * scale)), max(1, int(height * scale))),
        interpolation=cv2.INTER_AREA,
    )


def _heat(normalized: float) -> tuple[int, int, int]:
    amount = min(1.0, max(0.0, float(normalized)))
    blue = int(40 + 215 * amount)
    green = int(80 + 80 * amount)
    red = int(255 - 215 * amount)
    return blue, green, red


def _window_visibility() -> float | None:
    """Current window visibility, or None if the backend will not say yet."""

    try:
        return float(cv2.getWindowProperty(WINDOW_NAME, cv2.WND_PROP_VISIBLE))
    except cv2.error:
        return None


def _run_self_tests() -> None:
    run_calibration_self_test()
    run_tracker_self_test()
    run_osc_self_test()
    frame = make_fixture_frame()
    result = RadialTracker().process(frame, FIXTURE_CALIBRATION)
    view = render_view(frame, result, FIXTURE_CALIBRATION, True, True)
    plain = render_view(frame, result, FIXTURE_CALIBRATION, False, False)
    if view.shape[:2] != frame.shape[:2] or plain.shape[:2] != frame.shape[:2]:
        raise RuntimeError(f"mascara changed the frame size: feed {plain.shape}, mask {view.shape}")
    bar = TopBar().draw(view.shape[1], Session(calibration=FIXTURE_CALIBRATION, pick=True, status="self-test"))
    shown = np.vstack([bar, view])
    if shown.ndim != 3 or shown.shape[0] <= view.shape[0] or shown.shape[1] < 10:
        raise RuntimeError("debug view did not render")
    # Cocoa gives image pixels. Subtracting the window's screen position
    # (here y=1434) used to throw a fader click off the bar.
    screen_rect = (0, 1434, shown.shape[1], shown.shape[0])
    fader = _map_window_point(120, 42, shown.shape[1], shown.shape[0], screen_rect, True)
    if fader != (120, 42) or not (0 <= fader[1] < TopBar.height):
        raise RuntimeError(f"fader click mapped to {fader}")
    # Software adjustments have to be a no-op at the default sliders, including
    # when the camera control is absent (the self-test has no camera).
    untouched = apply_image_adjustments(frame, FIXTURE_CALIBRATION, HardwareSupport())
    if not np.array_equal(untouched, frame):
        raise RuntimeError("fixture calibration should not restyle the frame")
    print("self-test passed")


if __name__ == "__main__":
    sys.exit(main())
