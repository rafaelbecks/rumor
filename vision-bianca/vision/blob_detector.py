from dataclasses import dataclass
from typing import List, Tuple

import cv2
import numpy as np


@dataclass
class BlobDetection:
    """Representa un sujeto detectado mediante sustracción de fondo."""

    centroid: Tuple[int, int]
    bbox: Tuple[int, int, int, int]  # (x, y, w, h)
    area: float


class BlobDetector:
    """Detector de blobs basado en MOG2, filtros morfológicos y máscara de exclusión central."""

    def __init__(
        self,
        min_blob_area: float = 800.0,
        max_blob_area: float = 45000.0,
        bg_history: int = 500,
        bg_var_threshold: float = 25.0,
        ignore_center_mask_radius: int = 35,
    ) -> None:
        self.min_blob_area = float(min_blob_area)
        self.max_blob_area = float(max_blob_area)
        self.bg_history = int(bg_history)
        self.bg_var_threshold = float(bg_var_threshold)
        self.ignore_center_mask_radius = int(ignore_center_mask_radius)

        self._init_subtractor()
        self.kernel_open = cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (5, 5))
        self.kernel_close = cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (11, 11))
        self.last_fg_mask: np.ndarray = np.zeros((480, 640), dtype=np.uint8)

    def _init_subtractor(self) -> None:
        """Inicializa o reinicia el sustractor de fondo MOG2."""
        self.bg_subtractor = cv2.createBackgroundSubtractorMOG2(
            history=self.bg_history,
            varThreshold=self.bg_var_threshold,
            detectShadows=True,
        )

    def reset_background(self) -> None:
        """Recalibra el modelo de fondo desde cero."""
        self._init_subtractor()

    def detect(
        self,
        frame: np.ndarray,
        center: Tuple[int, int],
    ) -> Tuple[List[Tuple[int, int]], List[Tuple[int, int, int, int]]]:
        """
        Procesa el frame y devuelve:
          - Lista de centroides [(cx, cy), ...]
          - Lista de bounding boxes [(x, y, w, h), ...]
        """
        blurred = cv2.GaussianBlur(frame, (5, 5), 0)
        fg_mask = self.bg_subtractor.apply(blurred)

        # Eliminar sombras grises (valor 127 en MOG2), conservando solo primer plano (255)
        _, fg_mask = cv2.threshold(fg_mask, 200, 255, cv2.THRESH_BINARY)

        # Filtrado morfológico con kernel elíptico para limpiar ruido y rellenar huecos
        fg_mask = cv2.morphologyEx(fg_mask, cv2.MORPH_OPEN, self.kernel_open)
        fg_mask = cv2.morphologyEx(fg_mask, cv2.MORPH_CLOSE, self.kernel_close)

        # Crítico: Aplicar máscara circular negra en el centro para ignorar luminiscencia del objeto 3D
        if self.ignore_center_mask_radius > 0:
            cv2.circle(
                fg_mask,
                (int(center[0]), int(center[1])),
                self.ignore_center_mask_radius,
                0,
                thickness=-1,
            )

        self.last_fg_mask = fg_mask

        contours, _ = cv2.findContours(
            fg_mask, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE
        )

        centroids: List[Tuple[int, int]] = []
        bboxes: List[Tuple[int, int, int, int]] = []

        for contour in contours:
            area = cv2.contourArea(contour)
            if self.min_blob_area <= area <= self.max_blob_area:
                moments = cv2.moments(contour)
                if moments["m00"] > 0:
                    cx = int(moments["m10"] / moments["m00"])
                    cy = int(moments["m01"] / moments["m00"])
                else:
                    x, y, w, h = cv2.boundingRect(contour)
                    cx = x + w // 2
                    cy = y + h // 2

                x, y, w, h = cv2.boundingRect(contour)
                centroids.append((cx, cy))
                bboxes.append((x, y, w, h))

        return centroids, bboxes
