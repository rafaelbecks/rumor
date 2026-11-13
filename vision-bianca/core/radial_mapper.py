from dataclasses import dataclass, field
import math
from typing import List, Optional, Tuple


@dataclass
class SubjectPolarMetrics:
    """Métricas polares de un sujeto individual respecto al objeto central."""

    x: int
    y: int
    distance: float
    angle_deg: float
    normalized_proximity: float
    inside_active_zone: bool


@dataclass
class RadialFrameState:
    """Resultado global del mapeo radial en el frame actual."""

    closest_norm: float = 0.0
    closest_angle: float = 0.0
    crowd_count: int = 0
    crowd_energy: float = 0.0
    closest_subject: Optional[SubjectPolarMetrics] = None
    subjects: List[SubjectPolarMetrics] = field(default_factory=list)


class RadialMapper:
    """Calcula coordenadas polares, proximidad normalizada y métricas grupales hacia (center_x, center_y)."""

    def __init__(
        self,
        center_x: int = 320,
        center_y: int = 240,
        r_min: float = 40.0,
        r_max: float = 220.0,
    ) -> None:
        self.center_x = int(center_x)
        self.center_y = int(center_y)
        self.r_min = float(r_min)
        self.r_max = max(float(r_max), self.r_min + 1.0)
        self._last_closest_angle: float = 0.0

    def set_center(self, center_x: int, center_y: int) -> None:
        """Actualiza en vivo el centro del objeto 3D."""
        self.center_x = int(center_x)
        self.center_y = int(center_y)

    @staticmethod
    def _clamp(value: float, low: float = 0.0, high: float = 1.0) -> float:
        return max(low, min(high, value))

    def compute_norm_proximity(self, distance: float) -> float:
        """
        Normaliza la distancia entre r_max (0.0) y r_min (1.0):
        t = 1.0 - clamp((d - r_min) / (r_max - r_min), 0.0, 1.0)
        """
        span = self.r_max - self.r_min
        if span <= 1e-6:
            return 1.0 if distance <= self.r_min else 0.0
        ratio = (distance - self.r_min) / span
        return 1.0 - self._clamp(ratio, 0.0, 1.0)

    def evaluate(self, points: List[Tuple[int, int]]) -> RadialFrameState:
        """
        Evalúa la lista de puntos detectados (x, y) y calcula:
          - Distancia euclidiana y ángulo en grados [-180, 180] hacia (center_x, center_y)
          - Sujeto más cercano y su valor normalizado t en [0.0, 1.0]
          - Cantidad de sujetos activos dentro de r_max (crowd/count)
          - Energía grupal ponderada en [0.0, 1.0] (crowd/energy)
        """
        subjects: List[SubjectPolarMetrics] = []
        active_subjects: List[SubjectPolarMetrics] = []

        for px, py in points:
            dx = float(px - self.center_x)
            dy = float(py - self.center_y)
            distance = math.hypot(dx, dy)
            angle_deg = math.degrees(math.atan2(dy, dx))
            inside = distance <= self.r_max
            norm_t = self.compute_norm_proximity(distance) if inside else 0.0

            subject = SubjectPolarMetrics(
                x=int(px),
                y=int(py),
                distance=distance,
                angle_deg=angle_deg,
                normalized_proximity=norm_t,
                inside_active_zone=inside,
            )
            subjects.append(subject)
            if inside:
                active_subjects.append(subject)

        crowd_count = len(active_subjects)
        if crowd_count == 0:
            return RadialFrameState(
                closest_norm=0.0,
                closest_angle=self._last_closest_angle,
                crowd_count=0,
                crowd_energy=0.0,
                closest_subject= min(subjects, key=lambda s: s.distance) if subjects else None,
                subjects=subjects,
            )

        closest = min(active_subjects, key=lambda s: s.distance)
        self._last_closest_angle = closest.angle_deg

        # Energía grupal: combina la proximidad promedio con un refuerzo por presencia múltiple
        # normalizado en [0.0, 1.0]
        sum_prox = sum(s.normalized_proximity for s in active_subjects)
        mean_prox = sum_prox / crowd_count
        multi_person_boost = min(1.0, (crowd_count - 1) * 0.15)
        crowd_energy = self._clamp(mean_prox + (1.0 - mean_prox) * multi_person_boost * mean_prox, 0.0, 1.0)

        return RadialFrameState(
            closest_norm=float(closest.normalized_proximity),
            closest_angle=float(closest.angle_deg),
            crowd_count=int(crowd_count),
            crowd_energy=float(crowd_energy),
            closest_subject=closest,
            subjects=subjects,
        )
