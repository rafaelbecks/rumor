import argparse
import logging
from pathlib import Path
import signal
import sys
import time
from typing import Any, Dict, List, Tuple

import cv2
import numpy as np
import yaml

from core.curves import EMAFilter, evaluate_curve
from core.radial_mapper import RadialFrameState, RadialMapper
from network.osc_sender import OSCSender
from vision.blob_detector import BlobDetector
from vision.camera import ThreadedCamera
from vision.mp_detector import MediaPipeDetector

logging.basicConfig(
    level=logging.INFO,
    format="[%(asctime)s] [%(levelname)s] %(name)s: %(message)s",
    datefmt="%H:%M:%S",
)
logger = logging.getLogger("RadialTracker")


def load_config(config_path: Path) -> Dict[str, Any]:
    """Carga el archivo YAML de configuración."""
    if not config_path.exists():
        raise FileNotFoundError(f"No se encontró el archivo de configuración: {config_path}")
    with config_path.open("r", encoding="utf-8") as f:
        return yaml.safe_load(f)


def save_config(config_path: Path, config_data: Dict[str, Any]) -> None:
    """Guarda la configuración calibrada en disco manteniendo el orden de claves."""
    with config_path.open("w", encoding="utf-8") as f:
        yaml.safe_dump(config_data, f, sort_keys=False, allow_unicode=True)
    logger.info(
        f"Calibración guardada en {config_path} -> "
        f"center=({config_data['radial_zone']['center_x']}, {config_data['radial_zone']['center_y']})"
    )


def draw_debug_gui(
    frame: np.ndarray,
    radial_mapper: RadialMapper,
    ignore_radius: int,
    state: RadialFrameState,
    bboxes: List[Tuple[int, int, int, int]],
    smoothed_curve: float,
     smoothed_energy: float,
    fps: float,
    tracking_mode: str,
    curve_type: str,
) -> np.ndarray:
    """
    Renderiza la interfaz de calibración interactiva:
      - Anillo exterior r_max (verde)
      - Anillo interior r_min (rojo/magenta)
      - Área enmascarada del objeto 3D central (gris)
      - Vectores desde cada sujeto hasta el centro con su distancia y valor normalizado
      - HUD lateral con FPS reales, valor crudo norm y barra de progreso vertical para curve
    """
    canvas = frame.copy()
    h, w = canvas.shape[:2]
    cx, cy = radial_mapper.center_x, radial_mapper.center_y
    r_min = int(round(radial_mapper.r_min))
    r_max = int(round(radial_mapper.r_max))

    # 1. Área enmascarada del objeto 3D central (gris translúcido + borde gris)
    overlay = canvas.copy()
    if ignore_radius > 0:
        cv2.circle(overlay, (cx, cy), ignore_radius, (110, 110, 110), thickness=-1)
        cv2.addWeighted(overlay, 0.45, canvas, 0.55, 0, canvas)
        cv2.circle(canvas, (cx, cy), ignore_radius, (160, 160, 160), thickness=1, lineType=cv2.LINE_AA)

    # 2. Anillo exterior r_max (verde) y anillo interior r_min (rojo/magenta)
    cv2.circle(canvas, (cx, cy), r_max, (0, 220, 0), thickness=2, lineType=cv2.LINE_AA)
    cv2.circle(canvas, (cx, cy), r_min, (180, 0, 255), thickness=2, lineType=cv2.LINE_AA)

    # Cruz central del objeto 3D
    cv2.drawMarker(
        canvas,
        (cx, cy),
        (255, 255, 255),
        markerType=cv2.MARKER_CROSS,
        markerSize=12,
        thickness=1,
        line_type=cv2.LINE_AA,
    )

    # Etiquetas de anillos
    cv2.putText(
        canvas,
        f"R_max ({r_max}px)",
        (cx - 40, max(15, cy - r_max - 6)),
        cv2.FONT_HERSHEY_SIMPLEX,
        0.42,
        (0, 220, 0),
        1,
        cv2.LINE_AA,
    )
    cv2.putText(
        canvas,
        f"R_min ({r_min}px)",
        (cx - 35, max(15, cy - r_min - 6)),
        cv2.FONT_HERSHEY_SIMPLEX,
        0.4,
        (180, 0, 255),
        1,
        cv2.LINE_AA,
    )

    # 3. Dibujar bounding boxes sutiles de blobs
    for bx, by, bw, bh in bboxes:
        cv2.rectangle(canvas, (bx, by), (bx + bw, by + bh), (100, 180, 100), 1)

    # 4. Líneas desde cada sujeto detectado hasta el objeto central
    for subj in state.subjects:
        is_closest = (
            state.closest_subject is not None
            and subj.x == state.closest_subject.x
            and subj.y == state.closest_subject.y
            and subj.inside_active_zone
        )
        if subj.inside_active_zone:
            line_color = (0, 255, 255) if is_closest else (255, 200, 0)
            line_thickness = 2 if is_closest else 1
        else:
            line_color = (120, 120, 120)
            line_thickness = 1

        cv2.line(
            canvas,
            (subj.x, subj.y),
            (cx, cy),
            line_color,
            thickness=line_thickness,
            lineType=cv2.LINE_AA,
        )
        cv2.circle(canvas, (subj.x, subj.y), 6, line_color, thickness=-1, lineType=cv2.LINE_AA)

        label = f"d:{subj.distance:.0f}px | t:{subj.normalized_proximity:.2f}"
        cv2.putText(
            canvas,
            label,
            (subj.x + 8, subj.y - 8),
            cv2.FONT_HERSHEY_SIMPLEX,
            0.42,
            line_color,
            1,
            cv2.LINE_AA,
        )

    # 5. HUD lateral con FPS, métricas OSC y barra de progreso vertical para `curve`
    hud_w = 155
    hud_x = w - hud_w
    hud_overlay = canvas.copy()
    cv2.rectangle(hud_overlay, (hud_x, 0), (w, h), (20, 20, 20), thickness=-1)
    cv2.addWeighted(hud_overlay, 0.72, canvas, 0.28, 0, canvas)

    text_x = hud_x + 10
    cv2.putText(
        canvas,
        f"FPS: {fps:.1f}",
        (text_x, 26),
        cv2.FONT_HERSHEY_SIMPLEX,
        0.55,
        (0, 255, 128),
        2,
        cv2.LINE_AA,
    )
    cv2.putText(
        canvas,
        f"Mode: {tracking_mode}",
        (text_x, 48),
        cv2.FONT_HERSHEY_SIMPLEX,
        0.42,
        (210, 210, 210),
        1,
        cv2.LINE_AA,
    )
    cv2.putText(
        canvas,
        f"Curve: {curve_type}",
        (text_x, 66),
        cv2.FONT_HERSHEY_SIMPLEX,
        0.42,
        (210, 210, 210),
        1,
        cv2.LINE_AA,
    )
    cv2.putText(
        canvas,
        f"Norm : {state.closest_norm:.3f}",
        (text_x, 94),
        cv2.FONT_HERSHEY_SIMPLEX,
        0.46,
        (255, 255, 255),
        1,
        cv2.LINE_AA,
    )
    cv2.putText(
        canvas,
        f"Curve: {smoothed_curve:.3f}",
        (text_x, 116),
        cv2.FONT_HERSHEY_SIMPLEX,
        0.46,
        (0, 220, 255),
        1,
        cv2.LINE_AA,
    )
    cv2.putText(
        canvas,
        f"Angle: {state.closest_angle:.1f} deg",
        (text_x, 138),
        cv2.FONT_HERSHEY_SIMPLEX,
        0.44,
        (200, 200, 200),
        1,
        cv2.LINE_AA,
    )
    cv2.putText(
        canvas,
        f"Count: {state.crowd_count}",
        (text_x, 160),
        cv2.FONT_HERSHEY_SIMPLEX,
        0.44,
        (200, 200, 200),
        1,
        cv2.LINE_AA,
    )
    cv2.putText(
        canvas,
        f"Enrgy: {smoothed_energy:.3f}",
        (text_x, 182),
        cv2.FONT_HERSHEY_SIMPLEX,
        0.44,
        (200, 200, 200),
        1,
        cv2.LINE_AA,
    )

    # Barra de progreso vertical para el valor suavizado de `curve`
    bar_x1 = hud_x + 25
    bar_x2 = hud_x + 65
    bar_top = 210
    bar_bottom = h - 55
    bar_height = max(10, bar_bottom - bar_top)

    cv2.rectangle(canvas, (bar_x1, bar_top), (bar_x2, bar_bottom), (90, 90, 90), 1)
    fill_h = int(round(max(0.0, min(1.0, smoothed_curve)) * bar_height))
    if fill_h > 0:
        # Color interpolado de cian a magenta según intensidad de la curva
        r_col = int(255 * smoothed_curve)
        g_col = int(220 * (1.0 - 0.5 * smoothed_curve))
        b_col = 255
        cv2.rectangle(
            canvas,
            (bar_x1 + 1, bar_bottom - fill_h),
            (bar_x2 - 1, bar_bottom - 1),
            (b_col, g_col, r_col),
            thickness=-1,
        )

    # Indicador horizontal del valor crudo `norm` sobre la misma barra
    norm_y = bar_bottom - int(round(max(0.0, min(1.0, state.closest_norm)) * bar_height))
    cv2.line(canvas, (bar_x1 - 5, norm_y), (bar_x2 + 5, norm_y), (255, 255, 255), 2)

    cv2.putText(
        canvas,
        "CURVE",
        (bar_x1 - 2, bar_bottom + 18),
        cv2.FONT_HERSHEY_SIMPLEX,
        0.42,
        (0, 220, 255),
        1,
        cv2.LINE_AA,
    )

    # Leyenda inferior de controles
    cv2.putText(
        canvas,
        "[Click] Centro | [B] Reset BG | [S] Guardar | [Q/ESC] Salir",
        (10, h - 12),
        cv2.FONT_HERSHEY_SIMPLEX,
        0.42,
        (220, 220, 220),
        1,
        cv2.LINE_AA,
    )

    return canvas


def parse_args() -> argparse.Namespace:
    """Parsea argumentos de línea de comandos (--headless y --debug-gui)."""
    parser = argparse.ArgumentParser(
        description="Radial Proximity Tracker (RPi ARM64 Installation Node)"
    )
    mode_group = parser.add_mutually_exclusive_group()
    mode_group.add_argument(
        "--headless",
        action="store_true",
        help="Ejecuta en modo producción sin interfaz gráfica (ahorra CPU/GPU en Raspberry Pi).",
    )
    mode_group.add_argument(
        "--debug-gui",
        action="store_true",
        help="Ejecuta con ventana interactiva de calibración y visualización en tiempo real.",
    )
    parser.add_argument(
        "--config",
        type=str,
        default="config.yaml",
        help="Ruta al archivo de configuración YAML (por defecto: config.yaml).",
    )
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    config_path = Path(args.config).resolve()
    config = load_config(config_path)

    # Si no se especifica ningún flag, por defecto corre en headless a menos que se pida --debug-gui
    debug_gui = bool(args.debug_gui and not args.headless)

    cam_cfg = config.get("camera", {})
    track_cfg = config.get("tracking", {})
    radial_cfg = config.get("radial_zone", {})
    curve_cfg = config.get("response_curve", {})
    osc_cfg = config.get("osc", {})

    tracking_mode = str(track_cfg.get("mode", "hybrid")).strip().lower()
    ignore_radius = int(track_cfg.get("ignore_center_mask_radius", 35))
    curve_type = str(curve_cfg.get("type", "exponential")).strip().lower()
    curve_exponent = float(curve_cfg.get("exponent", 2.5))
    ema_alpha = float(curve_cfg.get("ema_alpha", 0.18))

    # 1. Inicializar componentes
    camera = ThreadedCamera(
        device_id=int(cam_cfg.get("device_id", 0)),
        width=int(cam_cfg.get("width", 640)),
        height=int(cam_cfg.get("height", 480)),
        fps=int(cam_cfg.get("fps", 30)),
        flip_horizontal=bool(cam_cfg.get("flip_horizontal", False)),
        reconnect_delay=2.0,
    )

    blob_detector = BlobDetector(
        min_blob_area=float(track_cfg.get("min_blob_area", 800)),
        max_blob_area=float(track_cfg.get("max_blob_area", 45000)),
        bg_history=int(track_cfg.get("bg_history", 500)),
        bg_var_threshold=float(track_cfg.get("bg_var_threshold", 25)),
        ignore_center_mask_radius=ignore_radius,
    )

    mp_detector = None
    if tracking_mode in ("mediapipe", "hybrid"):
        # En modo híbrido usamos frame_skip=2 en MediaPipe para garantizar 30 FPS estables en RPi
        mp_skip = 2 if tracking_mode == "hybrid" else 1
        mp_detector = MediaPipeDetector(frame_skip=mp_skip)

    radial_mapper = RadialMapper(
        center_x=int(radial_cfg.get("center_x", 320)),
        center_y=int(radial_cfg.get("center_y", 240)),
        r_min=float(radial_cfg.get("r_min", 40)),
        r_max=float(radial_cfg.get("r_max", 220)),
    )

    curve_ema = EMAFilter(alpha=ema_alpha, initial_value=0.0)
    energy_ema = EMAFilter(alpha=ema_alpha, initial_value=0.0)
    osc_sender = OSCSender(osc_cfg.get("targets", []))

    stop_requested = False

    def handle_signal(signum: int, _frame: Any) -> None:
        nonlocal stop_requested
        logger.info(f"Señal {signum} recibida. Cerrando aplicación...")
        stop_requested = True

    signal.signal(signal.SIGINT, handle_signal)
    signal.signal(signal.SIGTERM, handle_signal)

    window_name = "Radial Proximity Tracker - Debug & Calibration"
    if debug_gui:
        cv2.namedWindow(window_name, cv2.WINDOW_NORMAL)

        def on_mouse(event: int, x: int, y: int, _flags: int, _param: Any) -> None:
            if event == cv2.EVENT_LBUTTONDOWN:
                # Restringir clics fuera del panel HUD lateral
                frame_w = int(cam_cfg.get("width", 640))
                if x < frame_w - 155:
                    radial_mapper.set_center(x, y)
                    config["radial_zone"]["center_x"] = int(x)
                    config["radial_zone"]["center_y"] = int(y)
                    logger.info(f"Nuevo centro radial establecido en vivo: ({x}, {y})")

        cv2.setMouseCallback(window_name, on_mouse)

    camera.start()
    target_fps = max(1, int(cam_cfg.get("fps", 30)))
    frame_interval = 1.0 / target_fps
    fps_smoothed = float(target_fps)
    prev_time = time.perf_counter()

    logger.info(
        f"Sistema iniciado en modo {'DEBUG-GUI' if debug_gui else 'HEADLESS'} "
        f"(tracking={tracking_mode}, curve={curve_type})."
    )

    try:
        while not stop_requested:
            loop_start = time.perf_counter()
            dt = max(1e-6, loop_start - prev_time)
            prev_time = loop_start
            instant_fps = 1.0 / dt
            fps_smoothed = 0.9 * fps_smoothed + 0.1 * instant_fps

            grabbed, frame = camera.read()
            if not grabbed or frame is None:
                # Si la cámara está reconectando, el objetivo cae a 0.0 y la curva desciende suavemente con EMA
                empty_state = RadialFrameState(
                    closest_norm=0.0,
                    closest_angle=radial_mapper._last_closest_angle,
                    crowd_count=0,
                    crowd_energy=0.0,
                )
                smoothed_curve_val = curve_ema.update(0.0)
                smoothed_energy_val = energy_ema.update(0.0)
                osc_sender.send_telemetry(
                    closest_norm=0.0,
                    closest_curve=smoothed_curve_val,
                    closest_angle=empty_state.closest_angle,
                    crowd_count=0,
                    crowd_energy=smoothed_energy_val,
                )

                if debug_gui:
                    placeholder = np.zeros(
                        (int(cam_cfg.get("height", 480)), int(cam_cfg.get("width", 640)), 3),
                        dtype=np.uint8,
                    )
                    cv2.putText(
                        placeholder,
                        "Reconectando camara USB...",
                        (40, 240),
                        cv2.FONT_HERSHEY_SIMPLEX,
                        0.7,
                        (0, 100, 255),
                        2,
                        cv2.LINE_AA,
                    )
                    hud_frame = draw_debug_gui(
                        frame=placeholder,
                        radial_mapper=radial_mapper,
                        ignore_radius=ignore_radius,
                        state=empty_state,
                        bboxes=[],
                        smoothed_curve=smoothed_curve_val,
                        smoothed_energy=smoothed_energy_val,
                        fps=fps_smoothed,
                        tracking_mode=tracking_mode,
                        curve_type=curve_type,
                    )
                    cv2.imshow(window_name, hud_frame)
                    key = cv2.waitKey(30) & 0xFF
                    if key in (ord("q"), 27):
                        break
                else:
                    time.sleep(frame_interval)
                continue

            center_pt = (radial_mapper.center_x, radial_mapper.center_y)
            bboxes: List[Tuple[int, int, int, int]] = []

            # 2. Pipeline de detección según modo configurado
            if tracking_mode == "blob":
                detected_points, bboxes = blob_detector.detect(frame, center=center_pt)
            elif tracking_mode == "mediapipe":
                detected_points = (
                    mp_detector.detect(frame, center=center_pt, ignore_radius=ignore_radius)
                    if mp_detector is not None
                    else []
                )
            else:  # "hybrid"
                blob_points, bboxes = blob_detector.detect(frame, center=center_pt)
                if mp_detector is not None:
                    detected_points = mp_detector.refine_or_merge_hybrid(
                        frame=frame,
                        blob_centroids=blob_points,
                        center=center_pt,
                        ignore_radius=ignore_radius,
                    )
                else:
                    detected_points = blob_points

            # 3. Evaluación geométrica polar
            radial_state = radial_mapper.evaluate(detected_points)

            # 4. Evaluación de curva de transferencia y filtro EMA
            # Regla crítica: cuando crowd_count == 0, target_curve = 0.0 y EMAFilter desciende suavemente frame a frame
            if radial_state.crowd_count == 0:
                target_curve = 0.0
                target_energy = 0.0
            else:
                target_curve = evaluate_curve(
                    radial_state.closest_norm,
                    curve_type=curve_type,
                    exponent=curve_exponent,
                )
                target_energy = radial_state.crowd_energy

            smoothed_curve_val = curve_ema.update(target_curve)
            smoothed_energy_val = energy_ema.update(target_energy)

            # 5. Envío de mensajes OSC
            osc_sender.send_telemetry(
                closest_norm=radial_state.closest_norm,
                closest_curve=smoothed_curve_val,
                closest_angle=radial_state.closest_angle,
                crowd_count=radial_state.crowd_count,
                crowd_energy=smoothed_energy_val,
            )

            # 6. Renderizado condicional e interacción en modo Debug GUI
            if debug_gui:
                debug_frame = draw_debug_gui(
                    frame=frame,
                    radial_mapper=radial_mapper,
                    ignore_radius=ignore_radius,
                    state=radial_state,
                    bboxes=bboxes,
                    smoothed_curve=smoothed_curve_val,
                    smoothed_energy=smoothed_energy_val,
                    fps=fps_smoothed,
                    tracking_mode=tracking_mode,
                    curve_type=curve_type,
                )
                cv2.imshow(window_name, debug_frame)
                key = cv2.waitKey(1) & 0xFF
                if key in (ord("q"), 27):
                    logger.info("Salida solicitada por el usuario (Q/ESC).")
                    break
                elif key in (ord("b"), ord("B")):
                    blob_detector.reset_background()
                    logger.info("Modelo de fondo MOG2 reiniciado manualmente (Tecla B).")
                elif key in (ord("s"), ord("S")):
                    save_config(config_path, config)
            else:
                elapsed = time.perf_counter() - loop_start
                sleep_time = frame_interval - elapsed
                if sleep_time > 0.001:
                    time.sleep(sleep_time)

    finally:
        camera.stop()
        if mp_detector is not None:
            mp_detector.close()
        if debug_gui:
            cv2.destroyAllWindows()
        logger.info("Aplicación finalizada correctamente.")


if __name__ == "__main__":
    main()
