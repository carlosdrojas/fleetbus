# What is verified, and what is not

Every claim is **measured** (a command you can run), **in simulation** (the real logic on the host, not the board),
**TARGET** (a design number) or **UNVERIFIED** (written, needs hardware). Host numbers from the author's MacBook
(Apple silicon, macOS) on 2026-11-23.

## Verified by running it (host)

| Claim | How | Command |
|---|---|---|
| CAN messages pack and unpack as the DBC defines | 4 Unity tests on the generated codec: round-trips, signed fields, ids, range checks | `make host-test` (`test_can_msgs`) |
| Battery fault FSM, limp-home FSM, heartbeat monitor behave as specified | 20 Unity tests: debounce, hysteresis, latching, FAULT → SAFE escalation, saturating counters, INT32_MIN/MAX current, recovery hold, relapse, 32-bit tick wrap | `test_fsm` |
| The two nodes work together | 9 scenarios on a virtual bus with the real node classes: boot, heartbeat ids and 10 Hz rates, body fail-safe at power-up, overtemp → limp-home (220 ms in simulation), battery unplugged → limp-home (420 ms) and back (1100 ms), FaultEvent on entry then 1 Hz, TX-queue drop counting, unknown protocol version ignored | `test_node` |
| The display renderer draws correctly | 6 tests on a 160×128 framebuffer: every banner, clamping at extremes, 300 random incremental updates equal a fresh full draw, an unchanged frame costs one fill call, fed from real bus traffic | `test_gauge` |
| The C++ renderer draws exactly what the C one did | the four reference screens re-rendered after the conversion are byte-identical PNGs | `make gauge-shots`, `git status docs/img` |
| The laptop dashboard decodes the bus | 5 pytest tests: DBC decode, node liveness at the 500 ms timeout, simulator source (FAULT → LIMP_HOME, unplug), HTTP/SSE | `make py-test` |
| Firmware cross-compiles and links | arm-none-eabi-gcc 15.3, `-Os`, Cortex-M0+: battery node 12,052 B, body node 15,488 B of 128 KB flash; 13.7 KB of 32 KB SRAM; plus `bringup`, `can_selftest`, `io_selftest` | `make fw` |
| No C++ constructors run before `main()` | `.init_array` holds only the toolchain's `crtbegin` entry | `arm-none-eabi-objdump -h build/fw-mspm0/arm/mspm0g3507/node_body.elf` |

## Not verified (needs the board)

| Claim | Status | Bench step |
|---|---|---|
| Clock tree at 80 MHz from the PLL, 40 MHz crystal for CAN | UNVERIFIED | P2, P3 |
| UART console at 115200 | UNVERIFIED | P2 |
| Pin map (`bsp_pins.h`) matches the LaunchPad | UNVERIFIED (from TI examples) | P2–L3 |
| MCAN at 500 kbit/s, 87.5 % sample point | UNVERIFIED (arithmetic) | P3 |
| Bus-off recovery without an interrupt | UNVERIFIED | F4 |
| ADC scaling, servo PWM, lights | UNVERIFIED | P4 |
| ST7735R init sequence and SPI timing | UNVERIFIED | L3 |
| Every timing in `docs/measured.md` | TARGET until measured | P5, L3, F |
| Stack sizes and the 12 KB heap are enough | UNVERIFIED (high-water marks printed) | P5 |
| Watchdog resets a hung task | UNVERIFIED | not planned yet |

## What host tests can't catch

Clock and pin configuration, peripheral register sequences, interrupt and DMA behaviour, real bus electrical effects
(edges, noise, ACK timing), stack overflow under real interrupt load, and timing: the simulator steps in perfect 10 ms
ticks with zero execution time and a perfect bus. That's why every timing above is a TARGET until the bench measures it.
