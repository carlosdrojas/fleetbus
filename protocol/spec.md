# FBP — FleetBus Protocol

> **MOCK(M5).** Draft wire format. Becomes the normative spec, with golden vectors, in M5.

```
 0      2   3     4      5          9      11            11+n
 +------+---+-----+------+----------+------+-------------+--------+
 |magic |ver|type |src id|  seq u32 | len  |   payload   | CRC32C |
 +------+---+-----+------+----------+------+-------------+--------+
```
Little-endian. `magic` = 0xFB 0x01. CRC-32C covers everything before it.

Types: 1 TELEMETRY · 2 HEARTBEAT · 3 ACK (cumulative) · 4 NACK (selective) · 5 OTA_OFFER · 6 OTA_CHUNK · 7 OTA_STATUS.
