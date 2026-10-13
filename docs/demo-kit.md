# Demo kit: FleetBus at a career fair or convention

Goal: a recruiter walks up, touches something, and within 30 seconds sees the system react on a real CAN bus and on the
dashboard. Then they leave with a link. Nothing here is built yet; it depends on the bench milestones (P2–P7) working.

## What sits on the table

- **The board ("car in a box").** A plywood or acrylic base, about 30 × 40 cm, with both LaunchPads (on the phase 5 node
  boards if they exist, otherwise neat breadboards), the CAN bus as a visible labelled twisted pair with the two 120 Ω
  ends marked, the servo, the lights (LED2 on each LaunchPad, or bigger LEDs), and the CANable.
- **Things a visitor can touch, all safe:**
  - **Pack-voltage knob** (the pot on the battery node).
  - **"Unplug the battery ECU" toggle switch** in the battery node's CAN-H stub. Keep both 120 Ω terminators on
    nodes that stay connected (the CANable end and the body node end), so flipping it never unterminates the bus.
  - **A button that heats the thermistor** (a small resistor next to it), optional.
- **Laptop**, full-screen dashboard. Everything runs locally (Docker Postgres, ingest, API, gateway), so it needs no
  Wi-Fi. Powered from USB through one hub: no mains wiring on the board, and no car battery.
- **A printed card** with a QR code to the GitHub repo and the 3D bench page (share it publicly first: the artifact is
  private until you change that in its Share menu), plus three resume bullets with measured numbers.

## The interactions (each under a minute)

With Lite (the deliverable) the table runs 1, 2 and 5 plus the LCD on the body node and the web dashboard
(`tools/lite_dash.py --can ...`) on the laptop. 3 and 4 need the optional bootloader/OTA extension.


1. **Turn the knob down.** The battery node goes WARNING, then FAULT; the body node goes limp-home (servo stops, lights
   change); the dashboard raises an alert. (Simulation: fault alert in 1.3 s, `docs/verification.md`. Measure the real
   board before quoting a number.)
2. **Flip the "unplug" switch.** The body node loses the heartbeat and goes limp-home; the dashboard shows "node silent"
   (simulation: 3.0 s). Flip it back: everything recovers on its own.
3. *(Full version only: needs the bootloader/OTA extension, P6–P7. Not part of Lite.)* **Push a good update from the dashboard.** Canary first: one node downloads the signed image over CAN, reboots into
   the new slot as TRIAL, passes its health check and confirms, then the next node updates.
4. *(Full version only, as above.)* **Push a broken update.** The never-healthy image installs, fails its own 10 s health deadline, the watchdog resets
   it, and after 3 attempts the bootloader rolls back by itself. The rollout halts: fail-closed. This is the one
   recruiters remember.
5. **"What about a real car?"** The dashboard's car view replays a recorded Sentra drive (the trace from the day-one
   test). Show the allowlist and the CI guard that make the in-car node read-only.

## 60-second pitch

"This is a car's electronics and its cloud at desk scale. Two microcontrollers run FreeRTOS and talk over CAN like ECUs.
A gateway ships their data to a Go and Postgres backend over a UDP protocol I wrote, with sequence numbers and
retransmit, so nothing is lost when the link drops. The backend pushes signed firmware updates back down, one node at a
time, and only continues while the nodes stay healthy. Turn this knob and watch the body node go into limp-home. Now
watch me push a bad update: it rolls itself back without me doing anything."

## Making it survive a whole day

- **Reset between visitors:** one command that puts both nodes back on the known-good version (roll forward to a fresh
  version number, since anti-downgrade blocks going back) and clears alerts. To build: `scripts/expo.sh reset`.
- **Pre-signed images** for the good, never-healthy and hanging updates, built and signed before the event
  (`tools/sign_release.sh`).
- **Fallback:** if a board dies, `make demo` runs the same dashboard with simulated nodes running the real firmware
  logic. Say so when you switch.
- **Spares:** one spare LaunchPad already provisioned, spare transceiver, spare USB cables.
- **Kiosk view:** the dashboard in a large-type mode that cycles fleet → alerts → updates when nobody is touching it.
  To build.

## Extra parts for the kit

| Part | Qty | Why |
|---|---|---|
| Base board (plywood/acrylic ~30 × 40 cm), standoffs, cable ties, labels | 1 set | everything visible and fixed in place |
| Panel toggle switch (SPST) | 1 | the "unplug the battery ECU" switch in CAN-H |
| Panel-mount 10 kΩ pot with a big knob | 1 | replaces the breadboard pot so visitors can turn it |
| Powered USB hub | 1 | one cable from the laptop powers the whole board |
| Brighter LEDs + resistors (optional) | 2–4 | lights readable from a few metres away |
| Printed QR card / small stand | | link to the repo and the 3D page |

## What still has to be built (software)

Built since this was written: the Lite web dashboard (`tools/lite_dash.py`, `make lite-dash` for the simulator fallback)
and the LCD instrument cluster on the body node. Still to build:

- `scripts/expo.sh` with `up` (stack + real-bus gateway), `reset` and `fallback` (switch to simulated nodes).
- Dashboard kiosk mode (large type, auto-cycling views, an "update" button wired to the pre-signed images).
- Car-trace replay into the dashboard's car view (from `hil/traces/`).
