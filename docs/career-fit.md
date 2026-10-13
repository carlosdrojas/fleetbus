# Why FleetBus — career fit

Context: Tesla internship applications (mostly Winter/Spring 2027) and SpaceX. Full application context lives in `~/Documents/Dev/Apply/applications/_research/context-transfer.md`.

## The gap it closes
SWE roles are already covered by existing projects (FleetFlight, Fantasy GM, ARCA, PintOS). Embedded roles are not: no RTOS, no CAN, no firmware unit tests. Those three gaps block **6 of the top 20** Tesla roles in `Apply/applications/tracker.csv` (Optimus Embedded, Camera & Robotics, AI Platforms, Firmware Platforms, High Voltage, New Programs). FleetBus closes all three, plus the smaller SWE gaps.

| Gap | Roles it blocks | How FleetBus closes it | Phase |
|---|---|---|---|
| RTOS | All Tesla embedded | FreeRTOS tasks, queues, priorities + bare-metal bootloader | 1, 4 |
| CAN | All Tesla embedded, Factory Firmware | 2+ nodes, versioned `.dbc` protocol | 1 |
| Firmware unit tests | Firmware Platforms (required) | Unity tests in GitHub Actions | 2 |
| HIL testing | Update Systems Validation, Software Validation | Python harness replaying CAN traces into real boards | 2 |
| Databases / event-driven | AI Data Infra, Automated Diagnostics | Queue → Postgres | 3 |
| Monitoring "car to cloud" | AI Data Infra, SRE | Per-node health metrics + alerts | 3 |
| Security | Red Team, Product Security | Signed images, anti-downgrade | 4 |
| OTA / updates | Update Systems, Firmware Platforms | Staged rollout + rollback; in-car in 4b | 4, 4b |
| Networking (SpaceX) | SpaceX SWE | Custom binary UDP protocol | 3 |
| PCB / power design | SpaceX Engineering, Tesla electrical | Custom node board; 12 V automotive power for the in-car node | 5 |

## Not covered (minor)
- **Controls theory:** only #16 High Voltage (282227) in the top 20 asks. A motor node with a speed loop would dent it; a controls course matters more.
- **Camera/ML:** only #5 Camera & Robotics (283274), already covered as well as possible by existing ML research and vision projects.
- **Go / Scala / Kafka:** listed as alternatives in postings; Python/Java + Amazon SQS/SNS satisfy them.

## SpaceX
- **SpaceX Software Engineering:** strong fit (C on hardware, custom network protocol, testing, debugging).
- **SpaceX Engineering:** requires 3+ months of applied engineering; personal projects count. A flight computer is a bonus (domain fit + presentation story), not a requirement. Phase 5's custom PCB covers what avionics teams look for.
- **ITAR:** SpaceX requires US citizen / green card. Confirm before investing for SpaceX specifically.
- Every SpaceX loop ends with a 15–30 min project presentation to 5–10 engineers: depth, design decisions, failure analysis. FleetBus's rollback story is built for that.

## Timing
Don't wait on FleetBus to apply. SWE applications go out now. FleetBus matters for embedded interviews (Tesla grills your own project) and for Summer 2027 reqs (expected late Oct–Nov). On hardware, "FleetBus Lite" (port milestones P2–P5 on two MSPM0 LaunchPads: FreeRTOS + CAN + drivers) is the part that closes the RTOS/CAN/firmware-test gaps; then P6–P7 (bootloader + OTA). The rest of the software is already built and tested in simulation.

## Scope chosen: Lite (2026-10-13)
Lite (two MSPM0 LaunchPads, FreeRTOS + CAN, limp-home, LCD instrument cluster, web dashboard) closes the three embedded
gaps on hardware: RTOS, CAN, firmware unit tests. The OTA, update-systems, security and networking rows above are covered
only in simulation unless the P6–P8 extension is built; label them that way on a resume.

## Coverage with the other projects (2026-10-12)
SWE/infra and diagnostics are covered by FleetFlight, Fantasy GM, ARCA, PintOS and the Amazon internship; PCB/EE by the Guitar Hero and Space Invaders boards (measure the buck converter instead of doing phase 5). Only FleetBus covers RTOS + CAN + OTA. Space Invaders (EE319K) used the same MSPM0 chip family.

## Target resume bullets (targets, not claims — fill real numbers when built)
- Built a 2-node CAN network on FreeRTOS with a versioned DBC protocol and heartbeat-based fault propagation
- Wrote a bare-metal CAN bootloader with Ed25519-signed A/B images and automatic rollback on failed health checks
- Validated firmware with Unity tests in CI and a Python HIL harness replaying recorded CAN traces
- Streamed node and vehicle telemetry over a custom UDP protocol into Postgres with a live fleet dashboard
