# Mockup status

`main` is a **mockup of the finished repo**: every directory and file the final project will have, with stub bodies. Nothing here is a measured result. Real work happens on the stacked milestone branches below.

Every stub is tagged `MOCK(Mn)`. To see what a milestone still owes: `grep -rn "MOCK(M5)" .` (add `--exclude-dir=.git`).

## What actually works on `main` today
- `docker compose -f deploy/docker-compose.yml up -d --wait db` → Postgres 16 healthy (verified).
- Host build + smoke tests of `firmware/common` (CRC-32C check value, heartbeat pack/unpack, FSM, peer monitor): `cmake -S firmware -B build && cmake --build build && ctest --test-dir build` (verified; plain asserts, not the Unity suite).
- `go vet ./... && go build ./...` in `backend/`; `py_compile` on `gateway/`. Compile checks only.

## What is mocked
Everything else: the firmware has never been cross-compiled or flashed (no ARM toolchain yet), the DBC is a draft, the Python/Go FBP codecs use a placeholder CRC, the gateway sends 3 canned heartbeats, the API returns canned JSON, the dashboard is a static page, the signing tool and bootloader are empty, HIL tests are skipped.

## Milestone branches (stacked: each starts from the one before)
| Branch | Replaces | Model |
|---|---|---|
| `m1-can-contract` | `MOCK(M1)`: DBC, codegen, `can_msgs.*` | Sonnet |
| `m2-firmware-foundation` | `MOCK(M2)`: cross build, BSP, submodules, flash/listen tools | Sonnet |
| `m3-nodes-talk` | `MOCK(M3)`: battery/body apps, FSMs, heartbeat, IWDG | Sonnet |
| `m4-tested` | `MOCK(M4)`: Unity suite, HIL harness, sim nodes | Sonnet |
| `m5-fbp-protocol` | `MOCK(M5)`: spec, golden vectors, Py+Go codecs, fuzzing | Opus |
| `m6-gateway` | `MOCK(M6)` | Sonnet |
| `m7-ingest-storage` | `MOCK(M7)` | Sonnet |
| `m8-api-dashboard` | `MOCK(M8)` | Sonnet |
| `m9-bootloader` | `MOCK(M9)` | Opus |
| `m10-signed-images` | `MOCK(M10)` | Opus |
| `m11-ota-transfer` | `MOCK(M11)` | Opus |
| `m12-staged-rollout` | `MOCK(M12)` | Opus |
| `m13-in-car-node` | `MOCK(M13)` (optional) | Opus |
| `m14-custom-pcb` | `MOCK(M14)` (optional) | Sonnet |

A milestone is done when its `MOCK(Mn)` tags are gone, its verification from the plan passes with real output, and `[BENCH]` steps are marked done or deferred. Merge into `main` in order.
