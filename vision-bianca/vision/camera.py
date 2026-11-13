import logging
import platform
import threading
import time
from typing import Optional, Tuple

import cv2
import numpy as np

logger = logging.getLogger(__name__)


class ThreadedCamera:
    """Captura de video multihilo con reconexión automática para Raspberry Pi (V4L2) y multiplataforma."""

    def __init__(
        self,
        device_id: int = 0,
        width: int = 640,
        height: int = 480,
        fps: int = 30,
        flip_horizontal: bool = False,
        reconnect_delay: float = 2.0,
    ) -> None:
        self.device_id = device_id
        self.width = width
        self.height = height
        self.fps = fps
        self.flip_horizontal = flip_horizontal
        self.reconnect_delay = reconnect_delay

        self.cap: Optional[cv2.VideoCapture] = None
        self.frame: Optional[np.ndarray] = None
        self.grabbed: bool = False
        self.running: bool = False
        self._lock = threading.Lock()
        self._thread: Optional[threading.Thread] = None

    def _open_camera(self) -> bool:
        """Intenta inicializar el dispositivo de captura con backend óptimo (CAP_V4L2 en Linux)."""
        if self.cap is not None:
            try:
                self.cap.release()
            except Exception:
                pass
            self.cap = None

        system_name = platform.system().lower()
        if system_name == "linux":
            self.cap = cv2.VideoCapture(self.device_id, cv2.CAP_V4L2)
            if not self.cap.isOpened():
                self.cap = cv2.VideoCapture(self.device_id)
        else:
            self.cap = cv2.VideoCapture(self.device_id)

        if not self.cap or not self.cap.isOpened():
            logger.warning(f"No se pudo abrir la cámara (device_id={self.device_id}).")
            return False

        # Minimizar el buffer interno de V4L2 para reducir latencia en RPi
        self.cap.set(cv2.CAP_PROP_BUFFERSIZE, 1)
        self.cap.set(cv2.CAP_PROP_FRAME_WIDTH, self.width)
        self.cap.set(cv2.CAP_PROP_FRAME_HEIGHT, self.height)
        self.cap.set(cv2.CAP_PROP_FPS, self.fps)

        ret, initial_frame = self.cap.read()
        if not ret or initial_frame is None:
            logger.warning("Cámara abierta pero no entregó frame inicial.")
            self.cap.release()
            self.cap = None
            return False

        if self.flip_horizontal:
            initial_frame = cv2.flip(initial_frame, 1)

        with self._lock:
            self.grabbed = True
            self.frame = initial_frame

        logger.info(
            f"Cámara conectada exitosamente (device_id={self.device_id}, "
            f"{self.width}x{self.height} @ {self.fps}fps)."
        )
        return True

    def start(self) -> "ThreadedCamera":
        """Inicia el hilo de lectura continua."""
        if self.running:
            return self

        self.running = True
        self._open_camera()
        self._thread = threading.Thread(target=self._update, daemon=True, name="CameraThread")
        self._thread.start()
        return self

    def _update(self) -> None:
        """Loop del hilo secundario que lee frames y reconecta si la cámara falla."""
        while self.running:
            if self.cap is None or not self.cap.isOpened():
                with self._lock:
                    self.grabbed = False
                logger.warning(
                    f"Cámara desconectada. Reintentando conexión en {self.reconnect_delay}s..."
                )
                time.sleep(self.reconnect_delay)
                if self.running:
                    self._open_camera()
                continue

            ret, frame = self.cap.read()
            if not ret or frame is None:
                with self._lock:
                    self.grabbed = False
                logger.warning(
                    f"Lectura de frame fallida (None). Reconectando en {self.reconnect_delay}s..."
                )
                if self.cap is not None:
                    self.cap.release()
                    self.cap = None
                time.sleep(self.reconnect_delay)
                if self.running:
                    self._open_camera()
                continue

            if self.flip_horizontal:
                frame = cv2.flip(frame, 1)

            with self._lock:
                self.grabbed = True
                self.frame = frame

            # Pequeño respiro para ceder GIL sin afectar la tasa de captura
            time.sleep(0.001)

    def read(self) -> Tuple[bool, Optional[np.ndarray]]:
        """Devuelve una copia segura del frame más reciente."""
        with self._lock:
            if not self.grabbed or self.frame is None:
                return False, None
            return True, self.frame.copy()

    def stop(self) -> None:
        """Detiene el hilo de captura y libera el recurso de hardware."""
        self.running = False
        if self._thread is not None and self._thread.is_alive():
            self._thread.join(timeout=2.5)
        if self.cap is not None:
            try:
                self.cap.release()
            except Exception:
                pass
            self.cap = None
        logger.info("Cámara detenida y recursos liberados.")
