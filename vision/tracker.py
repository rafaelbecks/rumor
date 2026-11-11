"""White-anchor isolation and radial proximity of the objects around it.

The anchor is the largest bright, low-saturation contour, unless a blob has been
locked by a click. Distance is measured
from that contour's outer edge, not from its center:

    euclidean   = hypot(neighbor - anchor_centroid)
    radius      = anchor radius toward the neighbor
    gap         = max(0, euclidean - radius)             # 0 when touching
    normalized  = gap / max(1, zone_radius - radius)     # 1 on the zone circle
"""

from __future__ import annotations

import math
import time
import urllib.request
from dataclasses import dataclass, field, replace
from pathlib import Path

import cv2
import numpy as np

from calibration import Calibration

MAX_NEIGHBORS = 16
NEIGHBOR_MIN_AREA = 450
NEIGHBOR_MAX_FRAME_FRACTION = 0.72
ANCHOR_FRAGMENT_PX = 8.0
PICK_PAD = 4
_LOCK_GATE = 160.0
_SMOOTHING = 0.58
_SNAP_PX = 90.0

_K3 = cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (3, 3))
_K5 = cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (5, 5))
_K9 = cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (9, 9))

_HAND_MODEL_URL = (
    "https://storage.googleapis.com/mediapipe-models/"
    "hand_landmarker/hand_landmarker/float16/1/hand_landmarker.task"
)

FIXTURE_ANCHOR = (360.0, 260.0)
FIXTURE_CALIBRATION = Calibration(
    exposure=50,
    gain=0,
    contrast=1.0,
    brightness=0,
    gamma=1.0,
    min_brightness=150,
    max_saturation=60,
    min_blob_area=2000,
    max_blob_area=200000,
    max_radial_zone=340,
)


@dataclass
class Anchor:
    x: float
    y: float
    radius: float
    area: float
    contour: np.ndarray

    def edge_toward(self, px: float, py: float) -> tuple[float, float]:
        radius = directional_radius(self.contour, self.x, self.y, px, py)
        dx = px - self.x
        dy = py - self.y
        dist = math.hypot(dx, dy)
        if dist < 1e-3:
            return self.x, self.y
        scale = min(radius, dist) / dist
        return self.x + dx * scale, self.y + dy * scale


@dataclass
class Neighbor:
    x: float
    y: float
    nearest_x: float
    nearest_y: float
    raw_distance: float
    normalized: float
    angle_deg: float
    source: str
    contour: np.ndarray | None = None
    points: tuple[tuple[float, float], ...] = ()


@dataclass
class Candidate:
    """A detected blob the operator can click to lock as the anchor."""

    x: float
    y: float
    area: float
    contour: np.ndarray
    bbox: tuple[int, int, int, int]
    kind: str


@dataclass
class TrackResult:
    anchor: Anchor | None
    neighbors: list[Neighbor]
    white_mask: np.ndarray
    width: int
    height: int
    candidates: list[Candidate] = field(default_factory=list)
    locked: bool = False
    reference: bool = False
    smooth_norm: float | None = None
    smooth_raw: float | None = None
    smooth_angle: float | None = None


class RadialTracker:
    def __init__(self) -> None:
        self.hands_enabled = False
        self._hands: HandDetector | None = None
        self._sx: float | None = None
        self._sy: float | None = None
        self._lost = 0
        self._lock: tuple[float, float] | None = None
        self._frozen: Anchor | None = None
        self._candidates: list[Candidate] = []
        self._reference: np.ndarray | None = None

    @property
    def locked(self) -> bool:
        return self._lock is not None

    def select_at(self, x: float, y: float) -> bool:
        """Lock the blob under (x, y). A miss clears the lock and returns False."""

        hit = candidate_at(self._candidates, x, y)
        if hit is None:
            self.clear_lock()
            return False
        frozen = _build_anchor(hit.contour.copy(), hit.area)
        if frozen is None:
            self.clear_lock()
            return False
        self._lock = (frozen.x, frozen.y)
        self._frozen = frozen
        self._sx, self._sy = frozen.x, frozen.y
        self._lost = 0
        return True

    def capture_reference(self, frame: np.ndarray) -> None:
        """Remember this frame as the empty scene. Later motion is compared to it."""

        self._reference = _prepare_motion(frame)

    def clear_lock(self) -> None:
        self._lock = None
        self._frozen = None
        self._reference = None

    def close(self) -> None:
        if self._hands is not None:
            self._hands.close()

    def set_hands(self, enabled: bool) -> None:
        self.hands_enabled = enabled
        if not enabled:
            print("MediaPipe hands off")
            return
        if self._hands is None:
            print("Loading MediaPipe hands...")
            self._hands = HandDetector()
        if self._hands.available:
            print(f"MediaPipe hands on ({self._hands.kind})")
        else:
            print(f"MediaPipe hands unavailable: {self._hands.reason}")

    @property
    def hands_status(self) -> str:
        if not self.hands_enabled:
            return "off"
        if self._hands is None or not self._hands.available:
            reason = "" if self._hands is None else self._hands.reason
            text = reason or "unavailable"
            return text if len(text) <= 42 else text[:39] + "..."
        return "on"

    def process(self, frame: np.ndarray, cal: Calibration) -> TrackResult:
        cal = cal.clamped()
        image = _as_bgr(frame)
        height, width = image.shape[:2]
        hsv = cv2.cvtColor(image, cv2.COLOR_BGR2HSV)
        lab = cv2.cvtColor(image, cv2.COLOR_BGR2LAB)
        white_mask = isolate_white(hsv, lab, cal)
        candidates = _collect_candidates(image, hsv, white_mask, cal)
        self._candidates = candidates
        neighbors: list[Neighbor] = []
        anchor: Anchor | None = None
        if self._reference is not None and self._reference.shape[:2] != (height, width):
            self._reference = None
        if self._frozen is not None:
            anchor = self._frozen
            neighbors = self._neighbors_for(image, anchor, candidates, cal)
        else:
            chosen = _anchor_candidate(candidates, None, cal)
            if chosen is None:
                self._lost += 1
                if self._lost >= 8:
                    self._sx = None
                    self._sy = None
            else:
                built = _build_anchor(chosen.contour, chosen.area)
                if built is not None:
                    anchor = self._stabilize(built)
                    neighbors = self._neighbors_for(image, anchor, candidates, cal)
        return TrackResult(
            anchor=anchor,
            neighbors=neighbors,
            white_mask=white_mask,
            width=width,
            height=height,
            candidates=candidates,
            locked=self._lock is not None,
            reference=self._reference is not None,
        )

    def _neighbors_for(
        self,
        image: np.ndarray,
        anchor: Anchor,
        candidates: list[Candidate],
        cal: Calibration,
    ) -> list[Neighbor]:
        neighbors: list[Neighbor] = []
        if self._reference is not None:
            motion = _motion_contours(image, self._reference, anchor)
            neighbors.extend(
                _neighbors_from_contours(anchor, motion, cal, fragments=False, source="motion")
            )
        else:
            others = [
                item.contour
                for item in candidates
                if math.hypot(item.x - anchor.x, item.y - anchor.y) > 8.0
            ]
            neighbors.extend(_neighbors_from_contours(anchor, others, cal, fragments=True))
        if self.hands_enabled and self._hands is not None and self._hands.available:
            neighbors.extend(_neighbors_from_hands(anchor, self._hands.detect(image), cal))
        return _reduce_neighbors(neighbors)

    def _stabilize(self, anchor: Anchor) -> Anchor:
        if self._sx is None or self._sy is None:
            self._sx, self._sy = anchor.x, anchor.y
            self._lost = 0
            return anchor
        jump = math.hypot(anchor.x - self._sx, anchor.y - self._sy)
        if jump > max(_SNAP_PX, anchor.radius * 1.4):
            self._sx, self._sy = anchor.x, anchor.y
        else:
            self._sx = _SMOOTHING * anchor.x + (1.0 - _SMOOTHING) * self._sx
            self._sy = _SMOOTHING * anchor.y + (1.0 - _SMOOTHING) * self._sy
        self._lost = 0
        return replace(anchor, x=self._sx, y=self._sy)


class HandDetector:
    """Optional MediaPipe hand landmarks. Bodies still show up as blobs."""

    def __init__(self) -> None:
        self.available = False
        self.reason = "MediaPipe hands were not initialized"
        self.kind = ""
        self._solutions = None
        self._landmarker = None
        self._timestamp_ms = 0
        self._open()

    def close(self) -> None:
        if self._solutions is not None:
            self._solutions.close()
            self._solutions = None
        if self._landmarker is not None:
            self._landmarker.close()
            self._landmarker = None

    def detect(self, frame: np.ndarray) -> list[list[tuple[float, float]]]:
        if not self.available:
            return []
        try:
            return self._detect(frame)
        except Exception as exc:
            self.available = False
            self.reason = str(exc)
            print(f"MediaPipe hands stopped: {exc}")
            return []

    def _open(self) -> None:
        try:
            import mediapipe as mp
        except ImportError as exc:
            self.reason = f"mediapipe is not installed ({exc})"
            return
        if self._try_solutions(mp):
            return
        self._try_tasks(mp)

    def _try_solutions(self, mp) -> bool:
        solutions = getattr(mp, "solutions", None)
        hands_module = getattr(solutions, "hands", None) if solutions is not None else None
        if hands_module is None:
            return False
        try:
            self._solutions = hands_module.Hands(
                static_image_mode=False,
                max_num_hands=4,
                model_complexity=0,
                min_detection_confidence=0.5,
                min_tracking_confidence=0.5,
            )
        except Exception as exc:
            self.reason = f"mediapipe solutions hands failed ({exc})"
            self._solutions = None
            return False
        self.kind = "solutions"
        self.available = True
        self.reason = ""
        return True

    def _try_tasks(self, mp) -> bool:
        try:
            model_path = _ensure_hand_model()
            options = mp.tasks.vision.HandLandmarkerOptions(
                base_options=mp.tasks.BaseOptions(model_asset_path=str(model_path)),
                running_mode=mp.tasks.vision.RunningMode.VIDEO,
                num_hands=4,
                min_hand_detection_confidence=0.5,
                min_hand_presence_confidence=0.5,
                min_tracking_confidence=0.5,
            )
            self._landmarker = mp.tasks.vision.HandLandmarker.create_from_options(options)
            self._mp = mp
        except Exception as exc:
            self.reason = f"could not start the hand landmarker ({exc})"
            self._landmarker = None
            return False
        self.kind = "tasks"
        self.available = True
        self.reason = ""
        return True

    def _detect(self, frame: np.ndarray) -> list[list[tuple[float, float]]]:
        height, width = frame.shape[:2]
        if self._solutions is not None:
            rgb = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
            rgb.flags.writeable = False
            result = self._solutions.process(rgb)
            hands = []
            if result.multi_hand_landmarks:
                for hand in result.multi_hand_landmarks:
                    points = [(lm.x * width, lm.y * height) for lm in hand.landmark]
                    if len(points) >= 5:
                        hands.append(points)
            return hands

        rgb = np.ascontiguousarray(cv2.cvtColor(frame, cv2.COLOR_BGR2RGB))
        image = self._mp.Image(image_format=self._mp.ImageFormat.SRGB, data=rgb)
        self._timestamp_ms += 33
        result = self._landmarker.detect_for_video(image, self._timestamp_ms)
        hands = []
        for hand in result.hand_landmarks:
            points = []
            for landmark in hand:
                if landmark.x is None or landmark.y is None:
                    continue
                points.append((float(landmark.x) * width, float(landmark.y) * height))
            if len(points) >= 5:
                hands.append(points)
        return hands


def isolate_white(hsv: np.ndarray, lab: np.ndarray, cal: Calibration) -> np.ndarray:
    """Bright, low-saturation, low-chroma pixels. Colored highlights drop out."""

    saturation = hsv[:, :, 1]
    value = hsv[:, :, 2]
    lightness = lab[:, :, 0]
    chroma_a = lab[:, :, 1].astype(np.int16) - 128
    chroma_b = lab[:, :, 2].astype(np.int16) - 128
    chroma = np.maximum(np.abs(chroma_a), np.abs(chroma_b))
    chroma_limit = max(10, int(round(cal.max_saturation * 0.75)))
    selected = (
        (value >= cal.min_brightness)
        & (lightness >= cal.min_brightness)
        & (saturation <= cal.max_saturation)
        & (chroma <= chroma_limit)
    )
    mask = np.zeros(value.shape, np.uint8)
    mask[selected] = 255
    mask = cv2.GaussianBlur(mask, (5, 5), 0)
    _, mask = cv2.threshold(mask, 170, 255, cv2.THRESH_BINARY)
    mask = cv2.morphologyEx(mask, cv2.MORPH_OPEN, _K3, iterations=1)
    mask = cv2.morphologyEx(mask, cv2.MORPH_CLOSE, _K5, iterations=2)
    return mask


def measure_proximity(
    contour: np.ndarray,
    ax: float,
    ay: float,
    px: float,
    py: float,
    zone_radius: float,
) -> tuple[float, float] | None:
    """Return (gap_px, normalized) for one point, or None if it is outside the zone."""

    dist = math.hypot(px - ax, py - ay)
    if dist > zone_radius:
        return None
    if contour is not None and len(contour) >= 3:
        if cv2.pointPolygonTest(contour, (float(px), float(py)), False) >= 0:
            return 0.0, 0.0
    radius = directional_radius(contour, ax, ay, px, py)
    gap = max(0.0, dist - radius)
    span = max(1.0, zone_radius - radius)
    return gap, min(1.0, gap / span)


def directional_radius(
    contour: np.ndarray, ax: float, ay: float, px: float, py: float
) -> float:
    """Distance from the centroid to the contour along the ray toward (px, py)."""

    if contour is None or len(contour) == 0:
        return 0.0
    points = contour.reshape(-1, 2).astype(np.float64)
    if len(points) == 1:
        return float(math.hypot(points[0, 0] - ax, points[0, 1] - ay))
    dx = px - ax
    dy = py - ay
    norm = math.hypot(dx, dy)
    if norm < 1e-3:
        return float(np.hypot(points[:, 0] - ax, points[:, 1] - ay).max())
    ux, uy = dx / norm, dy / norm
    start = points
    end = np.roll(points, -1, axis=0)
    sx = end[:, 0] - start[:, 0]
    sy = end[:, 1] - start[:, 1]
    cross = ux * sy - uy * sx
    valid = np.abs(cross) > 1e-8
    qx = start[:, 0] - ax
    qy = start[:, 1] - ay
    t = np.full(len(points), np.inf)
    s = np.zeros(len(points))
    t[valid] = (qx[valid] * sy[valid] - qy[valid] * sx[valid]) / cross[valid]
    s[valid] = (qx[valid] * uy - qy[valid] * ux) / cross[valid]
    hits = valid & (t >= 0.0) & (s >= 0.0) & (s <= 1.0)
    if np.any(hits):
        return float(t[hits].min())
    vertex = np.hypot(points[:, 0] - ax, points[:, 1] - ay)
    return float(vertex.max()) if vertex.size else 0.0


def _lerp_angle(current: float, target: float, alpha: float) -> float:
    delta = (target - current + 180.0) % 360.0 - 180.0
    return (current + alpha * delta) % 360.0


class DistanceLerp:
    """Ease the nearest distance so a new blob does not snap the OSC value."""

    def __init__(self) -> None:
        self._norm: float | None = None
        self._raw: float | None = None
        self._angle: float | None = None

    def apply(self, result: TrackResult, amount: float, zone: float) -> TrackResult:
        if result.anchor is None:
            self._norm = self._raw = self._angle = None
            return replace(result, smooth_norm=-1.0, smooth_raw=-1.0, smooth_angle=-1.0)

        amount = min(1.0, max(0.0, float(amount)))
        span = max(0.0, float(zone) - result.anchor.radius)
        if result.neighbors:
            target_norm = float(result.neighbors[0].normalized)
            target_raw = float(result.neighbors[0].raw_distance)
            target_angle = float(result.neighbors[0].angle_deg)
        elif self._norm is None:
            return replace(result, smooth_norm=-1.0, smooth_raw=-1.0, smooth_angle=-1.0)
        else:
            target_norm, target_raw = 1.0, span
            target_angle = self._angle if self._angle is not None else 0.0

        if self._norm is None or amount <= 0.001:
            self._norm, self._raw, self._angle = target_norm, target_raw, target_angle
        else:
            alpha = max(0.04, (1.0 - amount) ** 2)
            self._norm += alpha * (target_norm - self._norm)
            self._raw = (self._raw if self._raw is not None else target_raw) + alpha * (
                target_raw - (self._raw if self._raw is not None else target_raw)
            )
            self._angle = _lerp_angle(
                self._angle if self._angle is not None else target_angle, target_angle, alpha
            )

        if not result.neighbors and self._norm is not None and self._norm >= 0.985:
            self._norm = self._raw = self._angle = None
            return replace(result, smooth_norm=-1.0, smooth_raw=-1.0, smooth_angle=-1.0)

        return replace(
            result,
            smooth_norm=self._norm,
            smooth_raw=self._raw,
            smooth_angle=self._angle,
        )


def angle_degrees(ax: float, ay: float, bx: float, by: float) -> float:
    """Clockwise degrees from image-right, because image y grows downward."""

    degrees = math.degrees(math.atan2(by - ay, bx - ax))
    if degrees < 0.0:
        degrees += 360.0
    return degrees


def make_fixture_frame() -> np.ndarray:
    """Synthetic scene: a larger red disk, a white anchor, and a blue rectangle."""

    frame = np.full((480, 640, 3), 28, np.uint8)
    cv2.circle(frame, (110, 110), 100, (30, 30, 235), -1, cv2.LINE_AA)
    cv2.circle(frame, (int(FIXTURE_ANCHOR[0]), int(FIXTURE_ANCHOR[1])), 60, (250, 250, 250), -1, cv2.LINE_AA)
    cv2.rectangle(frame, (460, 230), (540, 300), (230, 60, 40), -1)
    return cv2.GaussianBlur(frame, (0, 0), 1.15)


def run_self_test() -> None:
    _test_distance_math()
    frame = make_fixture_frame()
    result = RadialTracker().process(frame, FIXTURE_CALIBRATION)
    white_pixels = int(cv2.countNonZero(result.white_mask))
    _check(
        4000 <= white_pixels <= 25000,
        f"white mask has {white_pixels} px; the red disk may be leaking in or the anchor was missed",
    )
    _check(result.anchor is not None, "fixture anchor was not found")
    assert result.anchor is not None
    offset = math.hypot(result.anchor.x - FIXTURE_ANCHOR[0], result.anchor.y - FIXTURE_ANCHOR[1])
    _check(offset <= 12.0, f"anchor centroid drifted to ({result.anchor.x:.1f}, {result.anchor.y:.1f})")
    _check(
        math.hypot(result.anchor.x - 110, result.anchor.y - 110) > 180,
        "the larger red disk was selected as the anchor",
    )
    _check(40.0 <= result.anchor.radius <= 80.0, f"anchor radius {result.anchor.radius:.1f}")
    _check(result.neighbors, "no neighbors inside the radial zone")
    nearest = result.neighbors[0]
    _check(nearest.x > 430.0, f"nearest object is not the blue rectangle: {_format_neighbors(result)}")
    _check(
        0.0 < nearest.normalized < 0.45,
        f"nearest normalized distance {nearest.normalized:.3f} is outside the expected band",
    )
    _check(
        nearest.angle_deg <= 25.0 or nearest.angle_deg >= 335.0,
        f"nearest angle {nearest.angle_deg:.1f} is not to the right",
    )
    _check(
        any(item.x < 180.0 for item in result.neighbors),
        f"red disk was not measured as a neighbor: {_format_neighbors(result)}",
    )
    norms = [item.normalized for item in result.neighbors]
    _check(norms == sorted(norms), f"neighbors are not sorted: {norms}")

    lost = RadialTracker().process(np.zeros((480, 640, 3), np.uint8), FIXTURE_CALIBRATION)
    _check(lost.anchor is None and lost.neighbors == [], "a black frame produced an anchor")

    big = cv2.resize(frame, (1280, 720), interpolation=cv2.INTER_LINEAR)
    timer = RadialTracker()
    timer.process(big, FIXTURE_CALIBRATION)
    started = time.perf_counter()
    for _ in range(8):
        timer.process(big, FIXTURE_CALIBRATION)
    elapsed_ms = (time.perf_counter() - started) / 8.0 * 1000.0
    print(
        f"anchor ({result.anchor.x:.1f}, {result.anchor.y:.1f}) "
        f"r={result.anchor.radius:.1f} white={white_pixels}px"
    )
    print("neighbors: " + _format_neighbors(result))
    locked_tracker = RadialTracker()
    first = locked_tracker.process(frame, FIXTURE_CALIBRATION)
    blues = [item for item in first.candidates if item.x > 430]
    _check(blues, f"blue rectangle was not a clickable candidate: {len(first.candidates)} blobs")
    _check(locked_tracker.select_at(blues[0].x, blues[0].y), "click did not lock the blue rectangle")
    locked = locked_tracker.process(frame, FIXTURE_CALIBRATION)
    _check(locked.anchor is not None and locked.locked, "locked frame lost the anchor")
    assert locked.anchor is not None
    _check(locked.anchor.x > 430, f"lock did not follow the blue rectangle ({locked.anchor.x:.0f})")
    covered = frame.copy()
    cv2.rectangle(covered, (430, 200), (560, 330), (28, 28, 28), -1)
    held = locked_tracker.process(covered, FIXTURE_CALIBRATION)
    _check(
        held.anchor is not None and held.anchor.x > 430 and held.locked,
        "the picked object changed after it left the image",
    )
    _check(not locked_tracker.select_at(2, 470), "a background click should release the lock")
    released = locked_tracker.process(frame, FIXTURE_CALIBRATION)
    _check(
        released.anchor is not None and abs(released.anchor.x - FIXTURE_ANCHOR[0]) <= 12,
        "releasing the lock did not return to the white anchor",
    )
    _check(released.reference is False, "releasing the lock kept the reference frame")

    reference_tracker = RadialTracker()
    seen = reference_tracker.process(frame, FIXTURE_CALIBRATION)
    white = next(item for item in seen.candidates if item.kind == "white")
    _check(reference_tracker.select_at(white.x, white.y), "could not lock the white anchor")
    reference_tracker.capture_reference(frame)
    still = reference_tracker.process(frame, FIXTURE_CALIBRATION)
    _check(still.reference, "the reference frame was not held")
    _check(still.neighbors == [], f"an unchanged scene produced motion: {_format_neighbors(still)}")
    approached = frame.copy()
    cv2.circle(approached, (360, 120), 22, (40, 220, 60), -1)
    moved = reference_tracker.process(approached, FIXTURE_CALIBRATION)
    _check(moved.neighbors, "a new object against the reference was not measured")
    _check(
        moved.neighbors[0].source == "motion" and moved.neighbors[0].y < 180,
        f"motion did not track the new object: {_format_neighbors(moved)}",
    )
    _check(not reference_tracker.select_at(2, 470), "background click should drop the reference")
    _check(reference_tracker._reference is None, "the reference survived the background click")

    jumped = DistanceLerp()
    far = replace(result, neighbors=[replace(result.neighbors[0], normalized=0.8, raw_distance=80.0)])
    snapped = jumped.apply(far, 0.0, 340.0)
    _check(snapped.smooth_norm == 0.8, f"zero smoothing did not snap: {snapped.smooth_norm}")
    near = replace(far, neighbors=[replace(far.neighbors[0], normalized=0.2, raw_distance=20.0)])
    eased = jumped.apply(near, 0.85, 340.0)
    _check(
        eased.smooth_norm is not None and 0.2 < eased.smooth_norm < 0.8,
        f"smoothing did not ease the jump: {eased.smooth_norm}",
    )

    print(f"tracker {elapsed_ms:.1f} ms/frame at 1280x720")
    print("tracker self-test passed")


def _test_distance_math() -> None:
    contour = _circle_contour(100, 100, 50)
    gap, norm = _must(measure_proximity(contour, 100, 100, 180, 100, 150))
    _check(abs(gap - 30.0) < 2.0, f"gap was {gap:.2f}, expected 30")
    _check(abs(norm - 0.30) < 0.04, f"normalized was {norm:.3f}, expected 0.30")

    touching, touching_norm = _must(measure_proximity(contour, 100, 100, 150, 100, 150))
    _check(touching < 2.0 and touching_norm < 0.02, f"rim point was not contact: {touching:.2f}")

    inside, inside_norm = _must(measure_proximity(contour, 100, 100, 100, 100, 150))
    _check(inside == 0.0 and inside_norm == 0.0, "the centroid should be contact")

    _check(measure_proximity(contour, 100, 100, 260, 100, 150) is None, "outside the zone")

    on_zone, on_zone_norm = _must(measure_proximity(contour, 100, 100, 250, 100, 150))
    _check(abs(on_zone - 100.0) < 3.0, f"zone-edge gap was {on_zone:.2f}")
    _check(abs(on_zone_norm - 1.0) < 0.04, f"zone-edge normalized was {on_zone_norm:.3f}")

    _check(abs(angle_degrees(0, 0, 10, 0) - 0.0) < 1e-6, "angle right")
    _check(abs(angle_degrees(0, 0, 0, 10) - 90.0) < 1e-6, "angle down")
    _check(abs(angle_degrees(0, 0, -10, 0) - 180.0) < 1e-6, "angle left")
    _check(abs(angle_degrees(0, 0, 0, -10) - 270.0) < 1e-6, "angle up")

    square = np.array([[[0, 0]], [[100, 0]], [[100, 100]], [[0, 100]]], np.int32)
    edge_x, edge_y, edge_dist = _nearest_contour_point(square, -20, 50)
    _check(
        abs(edge_x) < 1.0 and abs(edge_y - 50.0) < 1.0 and abs(edge_dist - 20.0) < 1.0,
        f"nearest edge point was ({edge_x:.1f}, {edge_y:.1f}) at {edge_dist:.1f}px",
    )
    square_radius = directional_radius(square, 50, 50, -20, 50)
    _check(abs(square_radius - 50.0) < 1.5, f"square radius toward the left edge was {square_radius:.1f}")


def candidate_at(candidates: list[Candidate], x: float, y: float) -> Candidate | None:
    """Smallest blob whose padded square contains the point."""

    hits: list[Candidate] = []
    for candidate in candidates:
        left, top, width, height = candidate.bbox
        if (
            left - PICK_PAD <= x <= left + width + PICK_PAD
            and top - PICK_PAD <= y <= top + height + PICK_PAD
        ):
            hits.append(candidate)
    if not hits:
        return None
    hits.sort(key=lambda item: item.bbox[2] * item.bbox[3])
    return hits[0]


def _collect_candidates(
    frame: np.ndarray, hsv: np.ndarray, white_mask: np.ndarray, cal: Calibration
) -> list[Candidate]:
    whites = _contour_candidates(white_mask, cal, "white")
    secondary_mask = _neighbor_mask(frame, hsv, white_mask, cal)
    scratch = np.zeros(white_mask.shape, np.uint8)
    blobs = [
        candidate
        for candidate in _contour_candidates(secondary_mask, cal, "blob")
        if not _region_is_white(hsv, candidate.contour, cal, scratch)
    ]
    kept: list[Candidate] = []
    for candidate in whites + blobs:
        if any(math.hypot(candidate.x - item.x, candidate.y - item.y) < 36.0 for item in kept):
            continue
        kept.append(candidate)
    kept.sort(key=lambda item: item.area, reverse=True)
    return kept[:24]


def _contour_candidates(mask: np.ndarray, cal: Calibration, kind: str) -> list[Candidate]:
    contours, _ = cv2.findContours(mask, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
    frame_area = mask.shape[0] * mask.shape[1]
    area_cap = min(float(cal.max_blob_area), NEIGHBOR_MAX_FRAME_FRACTION * frame_area)
    found: list[Candidate] = []
    for contour in sorted(contours, key=cv2.contourArea, reverse=True)[:40]:
        area = float(cv2.contourArea(contour))
        if len(contour) < 5 or area < NEIGHBOR_MIN_AREA or area > area_cap:
            continue
        centroid = _contour_centroid(contour)
        if centroid is None:
            continue
        x, y, width, height = cv2.boundingRect(contour)
        found.append(
            Candidate(
                x=centroid[0],
                y=centroid[1],
                area=area,
                contour=contour.copy(),
                bbox=(int(x), int(y), int(width), int(height)),
                kind=kind,
            )
        )
    return found


def _anchor_candidate(
    candidates: list[Candidate], lock: tuple[float, float] | None, cal: Calibration
) -> Candidate | None:
    if not candidates:
        return None
    if lock is None:
        whites = [
            item
            for item in candidates
            if item.kind == "white" and cal.min_blob_area <= item.area <= cal.max_blob_area
        ]
        if not whites:
            return None
        return max(whites, key=lambda item: item.area)
    nearest = min(
        candidates,
        key=lambda item: (item.x - lock[0]) ** 2 + (item.y - lock[1]) ** 2,
    )
    dist = math.hypot(nearest.x - lock[0], nearest.y - lock[1])
    gate = max(_LOCK_GATE, math.hypot(nearest.bbox[2], nearest.bbox[3]) * 0.75)
    if dist <= gate:
        return nearest
    return None


def _build_anchor(contour: np.ndarray, area: float) -> Anchor | None:
    centroid = _contour_centroid(contour)
    if centroid is None:
        return None
    _center, radius = cv2.minEnclosingCircle(contour)
    return Anchor(
        x=centroid[0],
        y=centroid[1],
        radius=float(radius),
        area=area,
        contour=contour,
    )


def _neighbors_from_contours(
    anchor: Anchor,
    contours: list[np.ndarray],
    cal: Calibration,
    fragments: bool,
    source: str = "blob",
) -> list[Neighbor]:
    neighbors: list[Neighbor] = []
    for contour in contours:
        centroid = _contour_centroid(contour)
        if centroid is None:
            continue
        if fragments and _is_anchor_fragment(anchor, centroid[0], centroid[1]):
            continue
        nearest = _nearest_contour_point(contour, anchor.x, anchor.y)
        neighbor = _neighbor_from_points(
            anchor, centroid[0], centroid[1], nearest[0], nearest[1], cal, source, contour
        )
        if neighbor is not None:
            neighbors.append(neighbor)
    return neighbors


def _neighbors_from_hands(
    anchor: Anchor, hands: list[list[tuple[float, float]]], cal: Calibration
) -> list[Neighbor]:
    neighbors: list[Neighbor] = []
    for points in hands:
        if not points:
            continue
        cx = sum(point[0] for point in points) / len(points)
        cy = sum(point[1] for point in points) / len(points)
        nearest = min(points, key=lambda point: (point[0] - anchor.x) ** 2 + (point[1] - anchor.y) ** 2)
        neighbor = _neighbor_from_points(
            anchor,
            cx,
            cy,
            nearest[0],
            nearest[1],
            cal,
            "hand",
            None,
            tuple(points),
        )
        if neighbor is not None:
            neighbors.append(neighbor)
    return neighbors


def _neighbor_from_points(
    anchor: Anchor,
    cx: float,
    cy: float,
    px: float,
    py: float,
    cal: Calibration,
    source: str,
    contour: np.ndarray | None,
    points: tuple[tuple[float, float], ...] = (),
) -> Neighbor | None:
    zone = float(cal.max_radial_zone)
    candidates = [(px, py)]
    if math.hypot(cx - anchor.x, cy - anchor.y) <= zone:
        candidates.append((cx, cy))
    measured: tuple[float, float] | None = None
    chosen: tuple[float, float] | None = None
    for qx, qy in candidates:
        hit = measure_proximity(anchor.contour, anchor.x, anchor.y, qx, qy, zone)
        if hit is None:
            continue
        if measured is None or hit[0] < measured[0]:
            measured = hit
            chosen = (qx, qy)
    if measured is None or chosen is None:
        return None
    gap, normalized = measured
    return Neighbor(
        x=cx,
        y=cy,
        nearest_x=chosen[0],
        nearest_y=chosen[1],
        raw_distance=gap,
        normalized=normalized,
        angle_deg=angle_degrees(anchor.x, anchor.y, chosen[0], chosen[1]),
        source=source,
        contour=contour,
        points=points,
    )


def _prepare_motion(frame: np.ndarray) -> np.ndarray:
    gray = cv2.cvtColor(_as_bgr(frame), cv2.COLOR_BGR2GRAY)
    return cv2.GaussianBlur(gray, (0, 0), 4.0)


def _motion_contours(
    frame: np.ndarray, reference: np.ndarray, anchor: Anchor
) -> list[np.ndarray]:
    """Blobs that differ from the empty reference, with the anchor itself removed."""

    current = _prepare_motion(frame)
    if current.shape != reference.shape:
        return []
    changed = cv2.threshold(cv2.absdiff(current, reference), 26, 255, cv2.THRESH_BINARY)[1]
    changed = cv2.morphologyEx(changed, cv2.MORPH_OPEN, _K3, iterations=1)
    changed = cv2.morphologyEx(changed, cv2.MORPH_CLOSE, _K5, iterations=2)
    occupied = np.zeros(changed.shape, np.uint8)
    cv2.drawContours(occupied, [anchor.contour], -1, 255, -1)
    occupied = cv2.dilate(occupied, _K9, iterations=2)
    changed[occupied > 0] = 0
    contours, _hierarchy = cv2.findContours(changed, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
    kept = [contour for contour in contours if cv2.contourArea(contour) >= NEIGHBOR_MIN_AREA]
    kept.sort(key=cv2.contourArea, reverse=True)
    return kept[:24]


def _neighbor_mask(
    frame: np.ndarray, hsv: np.ndarray, white_mask: np.ndarray, cal: Calibration
) -> np.ndarray:
    gray = cv2.GaussianBlur(cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY), (5, 5), 0)
    edges = cv2.Canny(gray, 40, 120)
    edges = cv2.morphologyEx(edges, cv2.MORPH_CLOSE, _K5, iterations=2)
    edges = cv2.dilate(edges, _K3, iterations=1)

    sat_floor = int(np.clip(cal.max_saturation + 25, 8, 255))
    colored = cv2.inRange(hsv[:, :, 1], sat_floor, 255)
    colored = cv2.morphologyEx(colored, cv2.MORPH_OPEN, _K3, iterations=1)
    colored = cv2.morphologyEx(colored, cv2.MORPH_CLOSE, _K5, iterations=1)

    local = cv2.GaussianBlur(gray, (0, 0), 11)
    dark = cv2.threshold(cv2.subtract(local, gray), 18, 255, cv2.THRESH_BINARY)[1]
    dark = cv2.morphologyEx(dark, cv2.MORPH_OPEN, _K3, iterations=1)

    mask = cv2.bitwise_or(edges, colored)
    mask = cv2.bitwise_or(mask, dark)
    exclude = cv2.dilate(white_mask, _K9, iterations=1)
    mask[exclude > 0] = 0
    return mask


def _reduce_neighbors(neighbors: list[Neighbor]) -> list[Neighbor]:
    ordered = sorted(
        neighbors,
        key=lambda item: (0 if item.source == "hand" else 1, item.normalized, item.raw_distance),
    )
    kept: list[Neighbor] = []
    for neighbor in ordered:
        if any(math.hypot(neighbor.x - item.x, neighbor.y - item.y) < 40.0 for item in kept):
            continue
        kept.append(neighbor)
    kept.sort(key=lambda item: (item.normalized, item.raw_distance))
    return kept[:MAX_NEIGHBORS]


def _is_anchor_fragment(anchor: Anchor, x: float, y: float) -> bool:
    # Positive inside the polygon. A blob hugging the anchor boundary is the
    # same object, not a neighbor.
    signed = cv2.pointPolygonTest(anchor.contour, (float(x), float(y)), True)
    return signed >= -ANCHOR_FRAGMENT_PX


def _region_is_white(
    hsv: np.ndarray, contour: np.ndarray, cal: Calibration, scratch: np.ndarray
) -> bool:
    scratch[:] = 0
    cv2.drawContours(scratch, [contour], -1, 255, -1)
    if cv2.countNonZero(scratch) < 20:
        return False
    mean_saturation = cv2.mean(hsv[:, :, 1], scratch)[0]
    mean_value = cv2.mean(hsv[:, :, 2], scratch)[0]
    return mean_value >= cal.min_brightness - 20 and mean_saturation <= cal.max_saturation + 12


def _contour_centroid(contour: np.ndarray) -> tuple[float, float] | None:
    moments = cv2.moments(contour)
    if moments["m00"] <= 1e-6:
        return None
    return moments["m10"] / moments["m00"], moments["m01"] / moments["m00"]


def _nearest_contour_point(contour: np.ndarray, x: float, y: float) -> tuple[float, float, float]:
    """Closest point on the contour polygon, including points along an edge."""

    points = contour.reshape(-1, 2).astype(np.float64)
    if len(points) == 1:
        dist = float(math.hypot(points[0, 0] - x, points[0, 1] - y))
        return float(points[0, 0]), float(points[0, 1]), dist
    start = points
    end = np.roll(points, -1, axis=0)
    sx = end[:, 0] - start[:, 0]
    sy = end[:, 1] - start[:, 1]
    length_sq = sx * sx + sy * sy
    t = ((x - start[:, 0]) * sx + (y - start[:, 1]) * sy) / np.maximum(length_sq, 1e-9)
    t = np.clip(t, 0.0, 1.0)
    px = start[:, 0] + t * sx
    py = start[:, 1] + t * sy
    dist_sq = (px - x) ** 2 + (py - y) ** 2
    index = int(np.argmin(dist_sq))
    return float(px[index]), float(py[index]), float(math.sqrt(float(dist_sq[index])))


def _as_bgr(frame: np.ndarray) -> np.ndarray:
    if frame.ndim == 2:
        return cv2.cvtColor(frame, cv2.COLOR_GRAY2BGR)
    if frame.shape[2] == 4:
        return cv2.cvtColor(frame, cv2.COLOR_BGRA2BGR)
    return frame


def _circle_contour(cx: float, cy: float, radius: float, steps: int = 180) -> np.ndarray:
    angles = np.linspace(0.0, 2.0 * math.pi, steps, endpoint=False)
    points = np.stack(
        [cx + radius * np.cos(angles), cy + radius * np.sin(angles)],
        axis=1,
    )
    return np.round(points).astype(np.int32).reshape(-1, 1, 2)


def _ensure_hand_model() -> Path:
    destination = Path(__file__).resolve().parent / "models" / "hand_landmarker.task"
    if destination.exists() and destination.stat().st_size > 100_000:
        return destination
    destination.parent.mkdir(parents=True, exist_ok=True)
    print(f"Downloading the MediaPipe hand model to {destination} ...")
    request = urllib.request.Request(_HAND_MODEL_URL, headers={"User-Agent": "rumor-vision/1.0"})
    try:
        with urllib.request.urlopen(request, timeout=30) as response:
            payload = response.read()
    except Exception as exc:
        destination.unlink(missing_ok=True)
        raise RuntimeError(
            "could not download the hand landmarker model. "
            f"Blob tracking still works. ({exc})"
        ) from exc
    if len(payload) < 100_000:
        raise RuntimeError("the hand landmarker download was incomplete")
    destination.write_bytes(payload)
    return destination


def _format_neighbors(result: TrackResult) -> str:
    parts = []
    for item in result.neighbors:
        parts.append(
            f"{item.source} ({item.x:.0f},{item.y:.0f}) "
            f"norm={item.normalized:.2f} gap={item.raw_distance:.0f}px "
            f"angle={item.angle_deg:.0f}"
        )
    return "; ".join(parts) if parts else "none"


def _must(value: tuple[float, float] | None) -> tuple[float, float]:
    if value is None:
        raise RuntimeError("expected a proximity measurement")
    return value


def _check(condition: bool, detail: str) -> None:
    if not condition:
        raise RuntimeError(detail)


if __name__ == "__main__":
    run_self_test()
