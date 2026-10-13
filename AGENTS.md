# FleetBus — agent context

Shared by Claude Code, Codex CLI and Gemini CLI (`CLAUDE.md` is a symlink to this file). Overview in `README.md`; full design in `docs/proposal.md`.

## What this is
Carlos's personal embedded + backend project: TI MSPM0G3507 LaunchPad nodes (STM32 Nucleo-G431RB kept as a secondary build target) on FreeRTOS + CAN, a fleet backend, and signed, rollback-safe OTA updates, with read-only data from his 2025 Nissan Sentra SR. Purpose: close the job-application gaps (RTOS, CAN, firmware tests; plus databases, monitoring, networking, security) for Tesla and SpaceX internships. See `docs/career-fit.md`.

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
- 2026-10-12 — **Hardware change (Carlos): primary hardware is the TI LP-MSPM0G3507 LaunchPad (his embedded-systems class board), superseding the 2026-10-03 Nucleo-G431RB choice.** STM32 BSP stays as a secondary target and as the fallback if the P0 feasibility spike fails (bootloader size, Ed25519 verify time on Cortex-M0+). Plan, risks and milestones P0 to P8: `docs/mspm0-port-plan.md`. Not yet verified: any of it on hardware; resume wording still says STM32 until the port is recorded in `~/Documents/Dev/Apply/projects/fleetbus.md`.
- 2026-10-12 — Stack (full tradeoffs in `docs/decisions.md`): **D1** Python gateway (`python-can`) + Go backend, with shared FBP golden vectors to prevent drift · **D2** firmware built with CMake + arm-none-eabi-gcc, HAL/FreeRTOS/Monocypher/Unity as submodules (not CubeIDE, not Zephyr) · **D3** dashboard is a Vite + React + TS SPA over REST/SSE (Grafana optional later via `/metrics`) · defaults: Postgres 16, in-process bounded Go channel as queue, CRC-32C.
- 2026-10-12 — Workflow change (Carlos): instead of one milestone per session, the **whole implementation was built in one pass on `main`** and verified in simulation. The M0–M14 branches that existed briefly were mock-up staging and are obsolete. Commits on `main` were made by Claude at Carlos's request; pushing / creating the GitHub repo is still Carlos's.
- 2026-10-12 — D9: MSPM0 primary, boards as a build option (`FB_BOARD`), see `docs/decisions.md`.
- 2026-10-12 — D4–D8 (commit-gated ACKs + stored cursor, simulator = real firmware logic, 28 KB bootloader, fail-closed rollout gate, two timeouts): see `docs/decisions.md`.
- 2026-10-12 — Model use (superseded for this pass by the one-shot build above): Sonnet for routine milestones, Opus for the protocol/bootloader/OTA/safety ones; one milestone per session; no subagents by default. `claude-router` disabled (cost a lot, no measured benefit). See **Model & session workflow**.
- Flight computer: not required for SpaceX; only a later optional add-on if SpaceX Engineering becomes the top target.

## Status
- 2026-10-12: **Whole stack built on `main` and verified in simulation** (see `docs/verification.md`): CAN contract, firmware common (82 host tests), host simulator running the real firmware logic, FBP in Python + Go with golden vectors and fuzzing, gateway with disk spool and OTA relay, Go ingest/api/alerts/rollout engine/fbctl on Postgres (61 tests, -race), React dashboard, HIL harness, in-car node logic, 12 end-to-end scenarios. Firmware cross-compiles and links (bootloader 24.5 KB of 28, apps 37–44 KB of 48.6 KB). **Nothing has run on hardware yet** (parts on the way). CI workflow written, not yet run on GitHub.
- 2026-10-12: **P0 feasibility spike done without a board** (`docs/p0-feasibility.md`, `firmware/spike/p0/`): R1 to R5 pass, R6 (headless flash over XDS110) needs a board.
- 2026-10-12: **MSPM0 port P1-P8 built on `main`, one branch per milestone** (`p1-board-split` ... `p8-docs-ci`, see `docs/mspm0-port-plan.md`). `-DFB_BOARD=stm32g4|mspm0g3507`; TI MSPM0 SDK 2.11 subset vendored. MSPM0 bootloader 18,080 B of 28 KB, apps 28.7-29.4 KB of 48.6 KB (measured link). Host tests (85) pass on both flash maps; full e2e passes with `FB_BOARD=mspm0g3507` (12/12). Fixed on the way: STM32 build really at `-Os` now (bootloader 24.5 -> 17.6 KB), single verify per boot, node task stack too small for OTA verify (both boards), STM32 never left CAN bus-off, no first-boot provisioning (`tools/provision_meta.py`), MSPM0 erased-flash ECC reset risk (reads via the uncorrected alias). **Nothing has run on hardware.** Pinned 3D bench page: https://claude.ai/artifact/PJnjriXdyV3DgxsRAaq5EC (`docs/mspm0-bench.html`).
- 2026-10-12: **FleetBus Lite built on `main`** (branches `l1-lite-images`, `l2-lite-dash`, `l3-lcd-gauge`, `l4-lite-docs`), the first hardware goal: standalone battery/body images at 0x0 (no bootloader), live web dashboard (`make lite-dash`, sim or CANable), ST7735 instrument cluster on the body node (renderer host-tested, real output in `docs/img/`). Guide: `docs/lite.md`; dashboard options: `docs/lite-dashboards.md`. Nothing on hardware yet.
- Next: when the LaunchPads arrive, walk `docs/mspm0-bench-steps.md` branch by branch (P2 first: prove the flash path, R6); push to GitHub and get CI green; phase 5 PCB not started (optional).

## Next steps
1. Parts arrive → follow `docs/mspm0-bench-steps.md` milestone by milestone (car day-one test in `docs/bench-guide.md`); write real measurements into `docs/measured.md` and only then drop TARGET/ESTIMATED labels.
2. `gh repo create`, push, get `.github/workflows/ci.yml` green.
3. Replace the dev signing key (`tools/sign_image.py keygen --force`) before anything leaves the bench.
4. Resume bullets: upgrade only with measured numbers (rules in `~/Documents/Dev/Apply/projects/fleetbus.md`).

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
