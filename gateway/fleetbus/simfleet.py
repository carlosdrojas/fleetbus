"""Builds the default simulated fleet: a battery and a body node provisioned with signed v1 images."""
from __future__ import annotations

import os
from pathlib import Path

from nacl.signing import SigningKey

from .fbsim import SimFleet
from .image import build_image

def sim_key() -> SigningKey:
    """Ephemeral demo key unless FLEETBUS_SIM_KEY (hex seed) is set. The simulator trusts whatever key it is given."""
    seed = os.environ.get("FLEETBUS_SIM_KEY")
    return SigningKey(bytes.fromhex(seed)) if seed else SigningKey(bytes(range(32)))

def make_sim_fleet(version: int = 1, key: SigningKey | None = None) -> SimFleet:
    key = key or sim_key()
    body = lambda tag: bytes([0, version & 0xFF]) + tag * 2000      # byte0 = sim behaviour (0 healthy), rest is filler
    images = {1: build_image(body(b"B"), 1, 0, version, key), 2: build_image(body(b"D"), 2, 0, version, key)}
    return SimFleet(bytes(key.verify_key), images)
