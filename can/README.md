# CAN contract

`fleetbus.dbc` is the single source of truth. `tools/dbc2c.py` generates `firmware/common/can_msgs.{h,c}` from it
(cantools), and the laptop dashboard decodes the bus with the same file. Edit the DBC, rerun the generator, never edit
the generated files by hand.

Classic CAN 2.0A (11-bit ids), 500 kbit/s, little-endian signals. Lower id wins arbitration, so the heartbeats
outrank everything else.

| ID | Message | Sender | Rate | Payload |
|---|---|---|---|---|
| 0x101 / 0x102 | Heartbeat | battery / body | 10 Hz | node id, firmware version, uptime, state, protocol version |
| 0x200 | BatteryStatus | battery | 10 Hz | pack mV, mA (signed), temperature in 0.1 °C (signed), SoC % |
| 0x201 | BodyStatus | body | 10 Hz | lights bitmask, servo degrees, limp-home flag |
| 0x300 | FaultEvent | battery | on FAULT/SAFE, then 1 Hz | fault code, severity |

Every message carries `ProtoVer` (currently 1) in its last byte; a node drops frames with a version it doesn't know.

The heartbeat id is 0x100 + node id. With one shared 0x100, two nodes that start a heartbeat in the same bit time
both win arbitration on the id, then collide on the NodeId byte: a bit error, an error frame, a synchronised retry and
the same collision again.
