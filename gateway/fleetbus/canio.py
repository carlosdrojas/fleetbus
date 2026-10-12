"""CAN backends for the gateway: a real adapter through python-can, or the simulator."""
from __future__ import annotations

import time

class CanBackend:
    def poll(self, now_ms: int) -> list[tuple[int, int, bytes]]:
        """Frames received since the last call: [(timestamp_ms, can_id, data)]."""
        raise NotImplementedError
    def send(self, can_id: int, data: bytes) -> None:
        raise NotImplementedError

class PythonCanBackend(CanBackend):
    """Real bus. `read_only=True` opens the adapter silent/listen-only and makes send() impossible: this is the mode for
    anything pointed at the Sentra. (The in-car node never exposes the car's bus to the gateway anyway.)"""
    def __init__(self, interface: str, channel: str, bitrate: int = 500000, read_only: bool = False):
        import can
        self.read_only = read_only
        kwargs = dict(interface=interface, channel=channel, bitrate=bitrate)
        self.hw_listen_only = False
        if read_only:
            try:                                            # hardware listen-only where the adapter/driver supports it
                self.bus = can.Bus(**kwargs, state=can.BusState.PASSIVE)
                self.hw_listen_only = True
                return
            except (TypeError, NotImplementedError, ValueError):
                pass                                        # fall through: software lock only (verify the adapter on day one)
        self.bus = can.Bus(**kwargs)

    def poll(self, now_ms: int):
        out = []
        while True:
            m = self.bus.recv(timeout=0)
            if m is None:
                return out
            if not m.is_error_frame and not m.is_remote_frame:
                out.append((now_ms, m.arbitration_id, bytes(m.data)))

    def send(self, can_id: int, data: bytes) -> None:
        if self.read_only:
            raise PermissionError("gateway opened read-only: refusing to transmit on this bus")
        import can
        self.bus.send(can.Message(arbitration_id=can_id, data=data, is_extended_id=False))

class SimBackend(CanBackend):
    def __init__(self, fleet, drop_to_nodes: float = 0.0, drop_from_nodes: float = 0.0, seed: int = 3):
        import random
        self.fleet, self.rng = fleet, random.Random(seed)
        self.drop_to, self.drop_from = drop_to_nodes, drop_from_nodes   # bus fault injection (bench only)
        self.dropped = 0
    def poll(self, now_ms: int):
        out = []
        for f in self.fleet.step(now_ms):
            if self.drop_from and self.rng.random() < self.drop_from:
                self.dropped += 1
            else:
                out.append(f)
        return out
    def send(self, can_id: int, data: bytes) -> None:
        if self.drop_to and self.rng.random() < self.drop_to:
            self.dropped += 1
            return
        self.fleet.inject(can_id, data)


class SerialBackend(CanBackend):
    """The in-car path: Sentra OBD-II -> telematics node (listens, polls with its allowlist) -> this serial link.
    The gateway never has a transmit path to the car: send() is refused. Frames arrive already timestamped by arrival."""
    def __init__(self, port: str, baud: int = 115200, serial_obj=None):
        from .uartlink import Decoder
        if serial_obj is None:
            import serial
            serial_obj = serial.Serial(port, baud, timeout=0)
        self.ser, self.dec = serial_obj, Decoder()

    def poll(self, now_ms: int):
        data = self.ser.read(4096)
        return [(now_ms, cid, d) for cid, d in self.dec.feed(data)] if data else []

    OTA_TO_NODE = (0x400, 0x401)     # OtaCtrl / OtaData: addressed to the telematics node itself; its firmware never forwards link frames to the car bus

    def send(self, can_id: int, data: bytes) -> None:
        if can_id not in self.OTA_TO_NODE:
            raise PermissionError("in-car backend: only OTA frames for the telematics node may be sent; nothing else, and never toward the car bus")
        from .uartlink import encode
        self.ser.write(encode(can_id, data))
