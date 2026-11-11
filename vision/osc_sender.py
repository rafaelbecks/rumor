"""OSC messages for the anchor and the objects inside its radial zone.

Every frame sends three messages:

  /anchor/position  norm_x  norm_y
      Origin is the top-left of the frame. Both axes are 0.0–1.0.
      Sent as -1 -1 when the white anchor is lost.

  /radial/nearest   normalized_distance  raw_pixel_distance  angle_degrees
      The closest object inside the max radial zone.
      normalized_distance is 0.0 when that object touches the anchor edge
      and 1.0 when its nearest point sits on the zone circle.
      raw_pixel_distance is that same gap in pixels (0 when touching).
      angle_degrees is the direction from the anchor centroid to that
      nearest point: 0° image-right, 90° image-down, 180° image-left,
      270° image-up.
      Sent as -1 -1 -1 when the zone is empty.

  /radial/distance  normalized_distance
      The same normalized gap as the first value of /radial/nearest,
      sent alone. -1 when the zone is empty.

  /radial/frame     frame_distance
      That same gap as a fraction of the frame. 0.0 is contact with the
      anchor. 0.10 means the gap covers 10% of the longer frame side, which
      is how far the object sits inside the picture when it is near the
      circle. -1 when the zone is empty.

  /radial/objects   count  dist_1  dist_2  ...
      Normalized gaps of every object in the zone, closest first, up to 16.
      count is 0 when the zone is empty.
"""

from __future__ import annotations

import socket
import threading
import time
from dataclasses import replace

from pythonosc.dispatcher import Dispatcher
from pythonosc.osc_server import ThreadingOSCUDPServer
from pythonosc.udp_client import SimpleUDPClient

from tracker import MAX_NEIGHBORS, Anchor, Neighbor, TrackResult

Address = tuple[str, list]


class OscSender:
    def __init__(self, host: str, port: int):
        self.host = host
        self.port = int(port)
        self.errors = 0
        self._client = SimpleUDPClient(self.host, self.port)

    def send_result(self, result: TrackResult) -> None:
        try:
            for address, args in build_messages(result):
                self._client.send_message(address, args)
        except OSError:
            self.errors += 1


def build_messages(result: TrackResult) -> list[Address]:
    if result.anchor is None or result.width <= 0 or result.height <= 0:
        return [
            ("/anchor/position", [-1.0, -1.0]),
            ("/radial/nearest", [-1.0, -1.0, -1.0]),
            ("/radial/distance", [-1.0]),
            ("/radial/frame", [-1.0]),
            ("/radial/objects", [0]),
        ]

    norm_x = _unit(result.anchor.x / result.width)
    norm_y = _unit(result.anchor.y / result.height)
    neighbors = result.neighbors[:MAX_NEIGHBORS]
    smooth_norm = result.smooth_norm
    if smooth_norm is None:
        if not neighbors:
            nearest: list = [-1.0, -1.0, -1.0]
            distance = [-1.0]
            frame = [-1.0]
        else:
            closest = neighbors[0]
            nearest = [
                float(closest.normalized),
                float(closest.raw_distance),
                float(closest.angle_deg),
            ]
            distance = [float(closest.normalized)]
            frame = [float(closest.raw_distance / max(result.width, result.height, 1))]
    elif smooth_norm < 0.0 or result.smooth_raw is None or result.smooth_angle is None:
        nearest = [-1.0, -1.0, -1.0]
        distance = [-1.0]
        frame = [-1.0]
    else:
        nearest = [float(smooth_norm), float(result.smooth_raw), float(result.smooth_angle)]
        distance = [float(smooth_norm)]
        frame = [float(result.smooth_raw / max(result.width, result.height, 1))]
    objects: list = [len(neighbors), *[float(item.normalized) for item in neighbors]] if neighbors else [0]
    return [
        ("/anchor/position", [norm_x, norm_y]),
        ("/radial/nearest", nearest),
        ("/radial/distance", distance),
        ("/radial/frame", frame),
        ("/radial/objects", objects),
    ]


def _unit(value: float) -> float:
    return float(min(1.0, max(0.0, value)))


def run_self_test() -> None:
    empty = TrackResult(
        anchor=None,
        neighbors=[],
        white_mask=_blank_mask(),
        width=640,
        height=480,
    )
    empty_messages = dict(build_messages(empty))
    _check(empty_messages["/anchor/position"] == [-1.0, -1.0], "lost anchor position")
    _check(empty_messages["/radial/nearest"] == [-1.0, -1.0, -1.0], "empty nearest")
    _check(empty_messages["/radial/distance"] == [-1.0], "empty normalized distance")
    _check(empty_messages["/radial/frame"] == [-1.0], "empty frame distance")
    _check(empty_messages["/radial/objects"] == [0], "empty object list")

    anchor = Anchor(x=320.0, y=240.0, radius=60.0, area=10000.0, contour=_blank_mask())
    neighbor = Neighbor(
        x=460.0,
        y=240.0,
        nearest_x=430.0,
        nearest_y=240.0,
        raw_distance=40.0,
        normalized=0.25,
        angle_deg=0.0,
        source="blob",
    )
    live = TrackResult(
        anchor=anchor,
        neighbors=[neighbor],
        white_mask=_blank_mask(),
        width=640,
        height=480,
    )
    live_messages = dict(build_messages(live))
    _check(live_messages["/anchor/position"] == [0.5, 0.5], live_messages["/anchor/position"])
    _check(live_messages["/radial/nearest"] == [0.25, 40.0, 0.0], live_messages["/radial/nearest"])
    _check(live_messages["/radial/distance"] == [0.25], live_messages["/radial/distance"])
    _check(live_messages["/radial/frame"] == [40.0 / 640.0], live_messages["/radial/frame"])
    _check(live_messages["/radial/objects"] == [1, 0.25], live_messages["/radial/objects"])
    eased = replace(live, smooth_norm=0.5, smooth_raw=80.0, smooth_angle=90.0)
    eased_messages = dict(build_messages(eased))
    _check(eased_messages["/radial/distance"] == [0.5], eased_messages["/radial/distance"])
    _check(eased_messages["/radial/frame"] == [80.0 / 640.0], eased_messages["/radial/frame"])
    _check(eased_messages["/radial/nearest"] == [0.5, 80.0, 90.0], eased_messages["/radial/nearest"])

    received = _roundtrip([empty, live])
    by_address: dict[str, list[tuple]] = {}
    for address, args in received:
        by_address.setdefault(address, []).append(tuple(args))
    for required in ("/anchor/position", "/radial/nearest", "/radial/distance", "/radial/frame", "/radial/objects"):
        _check(len(by_address.get(required, [])) == 2, f"UDP missed {required}: {received}")
    nearest = by_address["/radial/nearest"]
    _check((-1.0, -1.0, -1.0) in nearest, f"empty nearest was not delivered: {nearest}")
    _check((0.25, 40.0, 0.0) in nearest, f"live nearest was not delivered: {nearest}")
    _check((-1.0,) in by_address["/radial/distance"], by_address["/radial/distance"])
    _check((0.25,) in by_address["/radial/distance"], by_address["/radial/distance"])
    _check((-1.0,) in by_address["/radial/frame"], by_address["/radial/frame"])
    _check((40.0 / 640.0,) in by_address["/radial/frame"], by_address["/radial/frame"])
    _check((0.5, 0.5) in by_address["/anchor/position"], by_address["/anchor/position"])
    _check((1, 0.25) in by_address["/radial/objects"], by_address["/radial/objects"])
    print("osc self-test passed")


def _roundtrip(results: list[TrackResult]) -> list[tuple[str, tuple]]:
    received: list[tuple[str, tuple]] = []
    dispatcher = Dispatcher()
    dispatcher.set_default_handler(lambda address, *args: received.append((address, args)))
    server = ThreadingOSCUDPServer(("127.0.0.1", 0), dispatcher)
    server.socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        sender = OscSender("127.0.0.1", server.server_address[1])
        for result in results:
            sender.send_result(result)
        deadline = time.monotonic() + 2.0
        while time.monotonic() < deadline and len(received) < 10:
            time.sleep(0.02)
    finally:
        server.shutdown()
        server.server_close()
        thread.join(timeout=2.0)
    return received


def _blank_mask():
    import numpy as np

    return np.zeros((4, 4), np.uint8)


def _check(condition: bool, detail: object) -> None:
    if not condition:
        raise RuntimeError(str(detail))


if __name__ == "__main__":
    run_self_test()
