# FBP — FleetBus Protocol v1

Custom binary protocol over UDP between the gateway(s) and the backend. One datagram = one packet. Little-endian throughout.

```
 off  size  field
   0    2   magic      0xFB 0xB5
   2    1   version    1
   3    1   type       see below
   4    1   src        sender id (gateway id 1..255; backend uses 0)
   5    1   flags      bit0 = retransmit (set on resent packets); others reserved, must be 0
   6    4   seq        per-sender, per-direction sequence number, starts at 1, increments by 1
  10    2   len        payload length
  12    n   payload
12+n    4   crc32c     CRC-32C (Castagnoli) of bytes [0, 12+n)
```
Max datagram 1200 bytes (fits a 1280-byte IPv6 minimum MTU), so `len` ≤ 1184. Decoders reject: short packet, bad magic, unknown version, length mismatch, bad CRC, nonzero reserved flag bits, `len` > 1184.

## Types
| # | Name | Direction | Payload |
|---|---|---|---|
| 1 | TELEMETRY | gw → be | `base_ts_ms u64 · count u8 · count × record` — record: `dt_ms u16 · can_id u32 (bit31 = extended) · dlc u8 (0..8) · data[dlc]`. Timestamp of a record = base + dt. |
| 2 | HEARTBEAT | gw → be | `uptime_s u32 · unacked u16 · spooled u32 · resent u32` |
| 3 | ACK | be → gw | `cum_ack u32` — every seq ≤ cum_ack has been received and delivered in order |
| 4 | NACK | be → gw | `n u8 · n × u32` — specific missing seqs (gaps seen while later packets arrived) |
| 5 | OTA_OFFER | be → gw | `node u8 · slot u8 · version u32 · size u32 · image_id u32` |
| 6 | OTA_CHUNK | be → gw | `node u8 · offset u32 · data[len-5]` |
| 7 | OTA_STATUS | gw → be | `node u8 · status u8 · next_offset u32 · detail u16` — status: 0 ACCEPTED, 1 PROGRESS, 2 TRANSFERRED, 3 FAILED, 4 VERIFY_FAILED, 5 CONFIRMED, 6 ROLLED_BACK |

## Reliability
- Gateway → backend (TELEMETRY/HEARTBEAT/OTA_STATUS) is reliable via seq numbers: the gateway keeps every packet in a retransmit window until covered by a cumulative ACK; it resends on NACK and on timeout (flag bit0 set). While the link is down, packets are spooled to disk and replayed in order.
- Backend: de-duplicates by (src, seq), delivers in order from a reorder buffer (window 256), emits ACK every 16 packets or 50 ms, and NACK for gaps.
- Backend → gateway control packets (OTA_*) use the backend's own seq space (src 0) and are acknowledged by OTA_STATUS, not ACK.

## Golden vectors
`golden_vectors.json` holds byte-exact packets. Both the Python and the Go test suites must encode each vector to exactly `wire_hex` and decode `wire_hex` back to the stated fields. Plus negative vectors that both decoders must reject. Regenerate only with `tools/gen_vectors.py` and review the diff.
