# Demo kit

For a career fair, a lab demo or a video. Depends on P5 and L3 working on the bench.

## On the table

- Both LaunchPads on one board, the bus visible and labelled, the servo, the display on the body node.
- **Things to touch:** the pot ("pack voltage") on the battery node, and the battery's reset button ("kill the battery
  ECU"). Both are safe at any time.
- The laptop with `tools/lite_dash.py` on the bus (CANable), or `make dash` (simulator) if a board dies. Say which
  one is running.

## The interactions

1. **Turn the knob down.** Battery WARNING, then FAULT; the body goes limp-home (red light, servo centres); the display
   banner changes; the dashboard shows both.
2. **Hold the battery's reset.** The body loses the heartbeat and goes limp-home in about half a second (quote the
   number from `docs/measured.md`, not this). Release it: the body recovers on its own after 1 s of healthy heartbeats.

## 60-second pitch (fill in measured numbers before using it)

"This is two ECUs from a car at desk scale. One monitors a battery, one runs the body: lights, a servo and an
instrument cluster. They're Cortex-M0+ microcontrollers running FreeRTOS, and they talk over CAN at 500 kilobits.
The messages are defined once in a DBC file, and the C code that packs them is generated from it. The logic is C++17,
written so it runs the same on the board and in host tests. The important rule is that silence is a fault: if the
battery ECU stops sending its heartbeat, the body can't trust it, so it goes into limp-home. Watch: I'll hold the
battery in reset. The body reacts in [measured] milliseconds. Let go, and it waits a full second of good heartbeats
before it trusts the battery again."

## Recording the video

Phone on a stand over the boards, the laptop dashboard in the corner of the frame, one take per interaction. Show the
console line `t=... LIMP_HOME peer=0` on screen when it happens.
