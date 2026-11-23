# FleetBus Lite: agent context

Shared by Claude Code, Codex CLI and Gemini CLI (`CLAUDE.md` and `GEMINI.md` are symlinks to this file). Overview and
layout in `README.md`.

## What this is
Carlos's embedded project: two TI LP-MSPM0G3507 LaunchPads (Cortex-M0+) run FreeRTOS and talk over 500 kbit/s CAN as a
battery-monitor ECU and a body-controller ECU, with limp-home, an ST7735R instrument cluster, and a laptop dashboard.
Node logic and app are C++17; board support, bench images and vendor code are C (`docs/cpp-firmware.md`).

## Rules
- **No hardware claim until it is measured.** A number goes in `docs/measured.md` first (date, board, method), then
  anywhere else. Simulator and host numbers are labelled as such. Never write a number Carlos didn't measure.
- **Lite only.** Bootloader, OTA, the gateway/backend and the car path were cut on 2026-11-23 and live on the
  `full-platform` branch. Don't bring them back into `main`.
- Fault injection (shorting the bus, pulling terminators) stays on the bench.
- Every real hardware bug gets written up in `docs/measured.md` → Bugs found on hardware: symptom, how it was found, root
  cause, fix.
- `can_msgs.{h,c}` are generated: edit `can/fleetbus.dbc` and run `tools/dbc2c.py`.
- Ask before any `git push`. Local commits are fine.

## Status
- 2026-11-23: scope cut to Lite; node logic, app, simulator and tests converted to C++17 (39 Unity tests + 5 Python
  tests pass; five MSPM0 images link). Next: P2 bring-up on the bench (`docs/mspm0-bench-steps.md`).
- 2026-11-06: fault and limp-home FSMs first moved to C++17 behind a C API.
- 2026-10-12: MSPM0 chosen as the target; firmware cross-compiled, never run on a board.

## Where things are
- Bench procedure per milestone: `docs/mspm0-bench-steps.md`. Measurements and hardware bugs: `docs/measured.md`.
- What is verified vs not: `docs/verification.md`. Decisions: `docs/decisions.md`.
- Resume record for this project (outside the repo): `~/Documents/Dev/Apply/projects/fleetbus.md`; update it through
  Apply's `/update-evidence` when a milestone lands with real numbers.
