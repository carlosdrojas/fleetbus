# FleetBus Lite: agent context

Shared by Claude Code, Codex CLI and Gemini CLI (`CLAUDE.md` and `GEMINI.md` are symlinks to this file). Overview,
layout and design notes in `README.md`.

## Rules
- **No hardware claim until it is measured.** A number goes in `docs/measured.md` first (date, board, method), then
  anywhere else. Host-test numbers are labelled as such. Never write a number Carlos didn't measure.
- **Lite only.** Bootloader, OTA, gateway/backend, dashboard, DBC codegen and the car path live on the `full-platform`
  branch. Don't bring them back into `main`.
- Fault injection (shorting the bus, pulling the pull-up) stays on the bench.
- Every real hardware bug gets written up in `docs/measured.md` → Bugs found on hardware: symptom, how it was found, root
  cause, fix.
- The wire format is `firmware/common/can_msgs.hpp`; change it and its byte-layout tests together.
- Ask before any `git push`. Local commits are fine.

## Status
- 2026-11-24: dashboard, host simulator, DBC and code generator cut; hand-written `can_msgs.hpp`. 40 Unity tests
  pass; five MSPM0 images link. Next: P2 bring-up (`docs/mspm0-bench-steps.md`).
- 2026-11-23: scope cut to Lite; node logic and app converted to C++17.
- 2026-10-12: MSPM0 chosen as the target; firmware cross-compiled, never run on a board.

## Where things are
- Bench procedure per milestone: `docs/mspm0-bench-steps.md`. Measurements and hardware bugs: `docs/measured.md`.
- Resume record for this project (outside the repo): `~/Documents/Dev/Apply/projects/fleetbus.md`; update it through
  Apply's `/update-evidence` when a milestone lands with real numbers.
