"""CAN message helpers: ids and codecs come straight from can/fleetbus.dbc (single source of truth)."""
from __future__ import annotations

from functools import lru_cache
from pathlib import Path

import cantools

DBC_PATH = Path(__file__).resolve().parents[2] / "can" / "fleetbus.dbc"

@lru_cache(maxsize=1)
def db():
    return cantools.database.load_file(str(DBC_PATH))

def msg(name: str):
    return db().get_message_by_name(name)

HEARTBEAT = msg("Heartbeat").frame_id
BATTERY_STATUS = msg("BatteryStatus").frame_id
BODY_STATUS = msg("BodyStatus").frame_id
FAULT_EVENT = msg("FaultEvent").frame_id
OTA_CTRL = msg("OtaCtrl").frame_id
OTA_DATA = msg("OtaData").frame_id
OTA_ACK = msg("OtaAck").frame_id
VEHICLE_OBD = msg("VehicleObd").frame_id
OTA_IDS = {OTA_CTRL, OTA_DATA, OTA_ACK}

def encode(name: str, **signals) -> tuple[int, bytes]:
    m = msg(name)
    sig = {"ProtoVer": 1} if any(s.name == "ProtoVer" for s in m.signals) else {}
    sig.update(signals)
    return m.frame_id, bytes(m.encode(sig, strict=True))

def decode(can_id: int, data: bytes) -> tuple[str, dict] | None:
    """Heartbeats arrive at 0x100 + node id (firmware/common/node_core.h FB_HB_ID) and decode with the 0x100 layout."""
    if can_id & ~0xF == HEARTBEAT:
        can_id = HEARTBEAT
    try:
        m = db().get_message_by_frame_id(can_id)
        return m.name, m.decode(bytes(data), decode_choices=False)
    except (KeyError, ValueError):
        return None
