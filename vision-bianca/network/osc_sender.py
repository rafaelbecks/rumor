import logging
from typing import Any, Dict, List

from pythonosc.udp_client import SimpleUDPClient

logger = logging.getLogger(__name__)


class OSCSender:
    """Despachador OSC multitarjet para motores de audio y video en tiempo real."""

    ADDR_CLOSEST_NORM = "/installation/closest/norm"
    ADDR_CLOSEST_CURVE = "/installation/closest/curve"
    ADDR_CLOSEST_ANGLE = "/installation/closest/angle"
    ADDR_CROWD_COUNT = "/installation/crowd/count"
    ADDR_CROWD_ENERGY = "/installation/crowd/energy"

    def __init__(self, targets_config: List[Dict[str, Any]]) -> None:
        self.clients: List[Dict[str, Any]] = []
        for target in targets_config or []:
            name = str(target.get("name", "osc_target"))
            ip = str(target.get("ip", "127.0.0.1"))
            port = int(target.get("port", 8000))
            try:
                client = SimpleUDPClient(ip, port)
                self.clients.append(
                    {
                        "name": name,
                        "ip": ip,
                        "port": port,
                        "client": client,
                    }
                )
                logger.info(f"Cliente OSC inicializado: {name} ({ip}:{port})")
            except Exception as exc:
                logger.error(f"Error al crear cliente OSC '{name}' ({ip}:{port}): {exc}")

    def send_telemetry(
        self,
        closest_norm: float,
        closest_curve: float,
        closest_angle: float,
        crowd_count: int,
        crowd_energy: float,
    ) -> None:
        """
        Emite las 5 direcciones OSC especificadas hacia todos los destinos configurados.
        """
        norm_val = float(round(closest_norm, 5))
        curve_val = float(round(closest_curve, 5))
        angle_val = float(round(closest_angle, 2))
        count_val = int(crowd_count)
        energy_val = float(round(crowd_energy, 5))

        for entry in self.clients:
            client: SimpleUDPClient = entry["client"]
            try:
                client.send_message(self.ADDR_CLOSEST_NORM, norm_val)
                client.send_message(self.ADDR_CLOSEST_CURVE, curve_val)
                client.send_message(self.ADDR_CLOSEST_ANGLE, angle_val)
                client.send_message(self.ADDR_CROWD_COUNT, count_val)
                client.send_message(self.ADDR_CROWD_ENERGY, energy_val)
            except Exception as exc:
                logger.debug(f"Fallo de envío UDP hacia {entry['name']}: {exc}")
