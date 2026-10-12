# FleetBus — agent context

Shared by Claude Code, Codex CLI and Gemini CLI (`CLAUDE.md` is a symlink to this file). Overview in `README.md`; full design in `docs/proposal.md`.

## What this is
Carlos's personal embedded + backend project: STM32 nodes on FreeRTOS + CAN, a fleet backend, and signed, rollback-safe OTA updates, with read-only data from his 2025 Nissan Sentra SR. Purpose: close the job-application gaps (RTOS, CAN, firmware tests; plus databases, monitoring, networking, security) for Tesla and SpaceX internships. See `docs/career-fit.md`.

## Rules (non-negotiable)
- **Never write to the car.** Only standard OBD-II diagnostic requests. No flashing, fuzzing, or injecting frames on the Sentra's bus or modules.
- **Carlos's own node in the car may take OTA updates** (phase 4b). It's his device and only reads from the car.
- **Fault injection and deliberately broken images stay on the bench** (the two desk nodes).
- **OBD-II is powered with the car off:** the in-car node must sleep or be unplugged when parked.
- In-car screens are glanceable only; a passenger operates/films; nothing mounted on the windshield.
- Every claim in docs or resume bullets is measured or labeled as a target/estimate.

## Decisions
- 2026-10-03 — Standalone personal project. **Not** an ECE 445L final project (ruled out by Carlos; don't suggest it).
- 2026-10-03 — Hardware: 2× STM32 Nucleo-G431RB (built-in CAN controller, Cortex-M4), SN65HVD230/TCAN1042 transceivers, a USB-CAN adapter (e.g. CANable) that doubles as gateway and HIL tool. Chosen over MSPM0 for Tesla/industry recognizability and FreeRTOS/Unity tooling.
- 2026-10-03 — Gateway → backend uses a **custom binary protocol over UDP** (sequence numbers, CRC, resend). This is the SpaceX SWE networking piece; don't swap it for MQTT/HTTP.
- 2026-10-03 — OTA design: Ed25519 signatures (e.g. Monocypher on the MCU), anti-downgrade, A/B slots, trial boot with a 10 s health window, staged rollout gated on health read from Postgres. Same fail-closed idea as Carlos's Amazon allowlist gating.
- 2026-10-03 — Real car = 2025 Nissan Sentra SR. Standard PIDs are guaranteed; whether passive sniffing works is unknown → day-one listen-only test.
- 2026-10-04 — Optional phase 4b: Carlos's own STM32 node rides in the car (12 V, Pi + phone hotspot link) and takes the same OTA.
- 2026-10-12 — Stack (full tradeoffs in `docs/decisions.md`): **D1** Python gateway (`python-can`) + Go backend, with shared FBP golden vectors to prevent drift · **D2** firmware built with CMake + arm-none-eabi-gcc, HAL/FreeRTOS/Monocypher/Unity as submodules (not CubeIDE, not Zephyr) · **D3** dashboard is a Vite + React + TS SPA over REST/SSE (Grafana optional later via `/metrics`) · defaults: Postgres 16, in-process bounded Go channel as queue, CRC-32C.
- 2026-10-12 — Implementation runs as milestones M0–M14 (`README.md` → Build order). Claude stops at each; Carlos commits and pushes.
- 2026-10-12 — Model use: Sonnet for routine milestones, Opus for the protocol/bootloader/OTA/safety ones; one milestone per session; no subagents by default. `claude-router` disabled (cost a lot, no measured benefit). See **Model & session workflow**.
- Flight computer: not required for SpaceX; only a later optional add-on if SpaceX Engineering becomes the top target.

## Status
- 2026-10-04: proposal + system map done. Nothing ordered or built. No git repo yet.
- 2026-10-10: parts on the way. On resumes now as "In Progress" with design-stage bullets only (`~/Documents/Dev/Apply/projects/fleetbus.md` has the per-phase rules for upgrading them). Goal: phase 1 working before SHPE National.
- 2026-10-12: M0 nearly done: layout, ADRs (`docs/decisions.md`), CI stubs, `docs/toolchain.md`, Postgres compose (verified healthy). Open: install `gcc-arm-embedded` cask (needs sudo), then `git init` + `gh repo create`.

## Next steps
1. Order parts (`docs/proposal.md` → Parts). Confirm prices first.
2. Day one: USB-CAN adapter on the Sentra's OBD-II port in listen-only mode; record what the car exposes.
3. Phase 1: FreeRTOS on the first Nucleo; draft `fleetbus.dbc`.
4. `git init` + public GitHub repo early (resumes will link it).

## Model & session workflow (Claude Code)
Goal: spend premium model usage only where a wrong call is expensive. Claude can't switch its own model, so it **tells Carlos** which one to use.
- **One milestone per session.** At the start of a milestone, say which model it calls for (table below). If the current model doesn't match, say so in one line and suggest `/model sonnet` or `/model opus` before doing any work.
- **At the end of a milestone:** give the usual report, update **Status**, then suggest `/clear`. The plan, `README.md` and this file carry the state, so don't rely on chat history. If the context grows past ~150k tokens mid-milestone, suggest `/compact`.
- **No subagents by default.** Don't use `claude-router` (disabled) or spawn implementer/reviewer agents. Build in the main session so Carlos can follow and explain every line.
- **Exceptions (offer them; run only when Carlos says yes):** (a) one read-only review of the security/safety diffs: M10 signature/anti-downgrade, M11 slot writes, M13 OBD allowlist; (b) building the gateway and Go backend in parallel after M5's golden vectors are fixed.

| Model | Milestones | Why |
|---|---|---|
| **Sonnet** | M0, M1, M2, M3, M4, M6, M7, M8, M14 | Scaffolding, codegen, BSP, FSMs, test harnesses, REST/SSE, dashboard: well-trodden code |
| **Opus** | M5, M9, M10, M11, M12, M13 | Wire protocol + resend, bootloader/A-B/power-loss, Ed25519 + anti-downgrade, OTA transfer, fail-closed rollout, never-write-to-car allowlist |

If a Sonnet milestone fails the same verification twice, or a design question comes up, stop and suggest switching to Opus for that piece rather than retrying a third time.

## Conventions
- Keep docs honest: label anything not yet measured as a target.
- When a decision is made or a phase finishes, update **Decisions** / **Status** here and `README.md`'s status line.
- Career context lives in `~/Documents/Dev/Apply/applications/_research/context-transfer.md`; keep the FleetBus row there in sync.
