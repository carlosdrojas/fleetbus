# FleetBus

A car's network and its cloud, at bench scale.

Two STM32 "ECUs" run FreeRTOS on a real CAN bus. A third node of your own rides in a 2025 Nissan Sentra and reads the car through the OBD-II port without ever writing to it. All three report to a fleet backend over a custom UDP protocol. The backend pushes signed firmware back down one node at a time, and the bootloader rolls back any version that fails its health check.

**Status:** mockup of the finished repo on `main` (stubs, not implemented); real work lands milestone by milestone on branches. See [`MOCKUP.md`](MOCKUP.md). Parts on the way (2026-10-12).

```
 ┌──────── bench car (desk) ────────┐      ┌──── real vehicle (read-only) ────┐
 │ Battery node      Body node      │      │ Sentra → OBD-II → your node      │
 │ STM32·FreeRTOS    STM32·FreeRTOS │      │ (12 V, updatable) → Pi + hotspot │
 │      └──── CAN 500 kbit/s ───┘   │      └───────────────┬──────────────────┘
 └──────────────┬───────────────────┘                      │
          USB-CAN adapter                                   │
                │                                           │
        Gateway (laptop, python-can)                        │
        custom binary protocol over UDP (seq #, CRC)        │
                │                                           │
 ┌──────────────┴──────────── fleet backend ────────────────┴───┐
 │ UDP ingest → queue → Postgres → API + alerts → dashboard     │
 │ OTA service: Ed25519-signed images, staged rollout,          │
 │ health-gated promotion, rollback (A/B slots on each node)    │
 └──────────────────────────────────────────────────────────────┘
```

## Docs
| File | What |
|---|---|
| [`AGENTS.md`](AGENTS.md) | Rules, decisions, status. Read first (symlinked as `CLAUDE.md`). |
| [`docs/proposal.md`](docs/proposal.md) | Full proposal: components, phases, parts, risks, the Sentra plan |
| [`docs/career-fit.md`](docs/career-fit.md) | Why this project: which job gaps it closes, roles covered, SpaceX notes |
| [`docs/system-map.html`](docs/system-map.html) | Source of the visual system map. Published (private): https://claude.ai/artifact/DNPALB9ohPg4tH5UAbDd4j |
| [`docs/bench-model.html`](docs/bench-model.html) | Source of the interactive 3D bench model (scenarios: fault, OTA, rollback). Published (private): https://claude.ai/artifact/2nuYU8zFx4izhWUNbpENom |

## Build order
| Phase | Weeks | Deliverable |
|---|---|---|
| 1 · Bus up | 1–2 | FreeRTOS on both nodes, versioned CAN messages (`.dbc`) |
| 2 · Tested | 2–3 | Unity tests in CI, Python replay harness (HIL) |
| 3 · Fleet | 3–5 | UDP gateway, Postgres, dashboard, alerts |
| 4 · OTA | 5–8 | Bootloader, signed images, A/B slots, rollback |
| 4b · In-car node (optional) | 8–10 | Your node in the Sentra, same OTA |
| 5 · Hardware (optional) | +3–4 | Custom KiCad node board, bring-up, measurements |

Each phase ends in something resume-worthy, so stopping early still pays off.

## Milestones
Implementation runs as M0–M14 (one session each; see `AGENTS.md` → Model & session workflow).

| Phase | Milestones |
|---|---|
| 1 · Bus up | M0 skeleton · M1 CAN contract · M2 firmware foundation · M3 nodes talk |
| 2 · Tested | M4 Unity in CI + HIL harness |
| 3 · Fleet | M5 FBP protocol · M6 gateway · M7 ingest + storage · M8 API, alerts, dashboard |
| 4 · OTA | M9 bootloader · M10 signed images · M11 OTA transfer · M12 staged rollout |
| 4b · In-car (optional) | M13 |
| 5 · Hardware (optional) | M14 |
