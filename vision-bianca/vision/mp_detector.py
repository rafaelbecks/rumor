import time
from pathlib import Path
from typing import List, Optional, Tuple

import cv2
import mediapipe as mp
import numpy as np
from mediapipe.tasks.python import vision

# Equivale a model_complexity=0 del API antiguo (Solutions).
_MODEL_PATH = Path(__file__).resolve().parent.parent / "models" / "pose_landmarker_lite.task"
_LEFT_HIP = int(vision.PoseLandmark.LEFT_HIP)
_RIGHT_HIP = int(vision.PoseLandmark.RIGHT_HIP)
_MODEL_URL = (
    "https://storage.googleapis.com/mediapipe-models/pose_landmarker/"
    "pose_landmarker_lite/float16/1/pose_landmarker_lite.task"
)


class MediaPipeDetector:
    """Detector corporal ligero con MediaPipe Pose Landmarker (modelo lite) optimizado para ARM64."""

    def __init__(
        self,
        min_detection_confidence: float = 0.5,
        min_tracking_confidence: float = 0.5,
        frame_skip: int = 1,
    ) -> None:
        self.min_detection_confidence = min_detection_confidence
        self.min_tracking_confidence = min_tracking_confidence
        self.frame_skip = max(1, int(frame_skip))
        self._frame_counter = 0
        self._last_hip_point: Optional[Tuple[int, int]] = None
        self._last_timestamp_ms = 0

        if not _MODEL_PATH.is_file():
            raise FileNotFoundError(
                f"No se encontró el modelo de pose en {_MODEL_PATH}. "
                f"Descárgalo desde {_MODEL_URL}"
            )

        options = vision.PoseLandmarkerOptions(
            base_options=mp.tasks.BaseOptions(model_asset_path=str(_MODEL_PATH)),
            running_mode=vision.RunningMode.VIDEO,
            num_poses=1,
            min_pose_detection_confidence=self.min_detection_confidence,
            min_pose_presence_confidence=self.min_detection_confidence,
            min_tracking_confidence=self.min_tracking_confidence,
        )
        self.pose = vision.PoseLandmarker.create_from_options(options)

    def detect(
        self,
        frame: np.ndarray,
        center: Optional[Tuple[int, int]] = None,
        ignore_radius: int = 0,
    ) -> List[Tuple[int, int]]:
        """
        Extrae el punto medio de las caderas (landmarks 23 y 24) en coordenadas de píxeles.
        Si está dentro de la máscara de exclusión central, lo ignora.
        """
        self._frame_counter += 1
        if self.frame_skip > 1 and (self._frame_counter % self.frame_skip != 0):
            return [self._last_hip_point] if self._last_hip_point is not None else []

        height, width = frame.shape[:2]
        rgb_frame = np.ascontiguousarray(cv2.cvtColor(frame, cv2.COLOR_BGR2RGB))
        mp_image = mp.Image(image_format=mp.ImageFormat.SRGB, data=rgb_frame)
        results = self.pose.detect_for_video(mp_image, self._next_timestamp_ms())

        if not results.pose_landmarks:
            self._last_hip_point = None
            return []

        landmarks = results.pose_landmarks[0]
        left_hip = landmarks[_LEFT_HIP]
        right_hip = landmarks[_RIGHT_HIP]

        if self._visibility(left_hip) < 0.35 and self._visibility(right_hip) < 0.35:
            self._last_hip_point = None
            return []

        mid_x = int(((left_hip.x + right_hip.x) * 0.5) * width)
        mid_y = int(((left_hip.y + right_hip.y) * 0.5) * height)

        mid_x = max(0, min(width - 1, mid_x))
        mid_y = max(0, min(height - 1, mid_y))

        if center is not None and ignore_radius > 0:
            dx = mid_x - center[0]
            dy = mid_y - center[1]
            if (dx * dx + dy * dy) <= (ignore_radius * ignore_radius):
                self._last_hip_point = None
                return []

        self._last_hip_point = (mid_x, mid_y)
        return [self._last_hip_point]

    def refine_or_merge_hybrid(
        self,
        frame: np.ndarray,
        blob_centroids: List[Tuple[int, int]],
        center: Tuple[int, int],
        ignore_radius: int = 0,
        merge_distance_px: float = 95.0,
    ) -> List[Tuple[int, int]]:
        """
        Modo híbrido: utiliza los blobs de OpenCV como detector primario de múltiples sujetos
        y emplea MediaPipe (con salto de frames adaptativo para proteger los FPS en ARM64)
        para refinar la posición del sujeto principal o añadirlo si quedó estático frente al fondo.
        """
        mp_points = self.detect(frame, center=center, ignore_radius=ignore_radius)
        if not mp_points:
            return list(blob_centroids)

        mp_pt = mp_points[0]
        if not blob_centroids:
            return [mp_pt]

        merged = list(blob_centroids)
        # Buscar el blob más cercano a la detección de caderas de MediaPipe
        closest_idx = -1
        min_dist_sq = merge_distance_px * merge_distance_px

        for idx, (bx, by) in enumerate(merged):
            dist_sq = (bx - mp_pt[0]) ** 2 + (by - mp_pt[1]) ** 2
            if dist_sq <= min_dist_sq:
                min_dist_sq = dist_sq
                closest_idx = idx

        if closest_idx >= 0:
            # Refinar el centroide del blob principal promediando con la cadera de MediaPipe
            bx, by = merged[closest_idx]
            refined_x = int(round(0.4 * bx + 0.6 * mp_pt[0]))
            refined_y = int(round(0.4 * by + 0.6 * mp_pt[1]))
            merged[closest_idx] = (refined_x, refined_y)
        else:
            # Si el sujeto se quedó quieto y MOG2 lo absorbió, MediaPipe mantiene su presencia
            merged.append(mp_pt)

        return merged

    def close(self) -> None:
        """Libera los recursos del grafo de MediaPipe."""
        if self.pose is not None:
            self.pose.close()
            self.pose = None

    def _next_timestamp_ms(self) -> int:
        now_ms = int(time.monotonic() * 1000)
        if now_ms <= self._last_timestamp_ms:
            now_ms = self._last_timestamp_ms + 1
        self._last_timestamp_ms = now_ms
        return now_ms

    @staticmethod
    def _visibility(landmark: object) -> float:
        visibility = getattr(landmark, "visibility", None)
        if visibility is not None:
            return float(visibility)
        presence = getattr(landmark, "presence", None)
        if presence is not None:
            return float(presence)
        return 1.0
