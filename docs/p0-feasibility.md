# P0 feasibility spike: MSPM0G3507 port

Run 2026-10-12, no board on hand. Plan: `docs/mspm0-port-plan.md`. Spike code: `firmware/spike/p0/` (rebuild with `firmware/spike/p0/build.sh`; emulator harness `bench_emu.py` needs `unicorn==2.1.3` and `pyelftools`, since unicorn 2.1.1 crashes on this Mac).

Labels: **MEASURED** = produced by a build or an emulator run in this spike. **T1** = read in a TI document (datasheet SLASEX6C rev Oct 2025, TRM SLAU846E). **EST** = estimate, to be replaced by a bench measurement. Nothing here has run on a LaunchPad.

## Gate result

| Risk | Question | Answer | Status |
|---|---|---|---|
| R1 | Does the bootloader fit 28 KB on Cortex-M0+? | Yes at `-Os` (EST 18.4 to 22.6 KB). No at today's flags. | **Pass**, with a build-flag fix |
| R2 | How long does Ed25519 verify take? | 0.28 s per signature check, 0.43 s per full image verify at 80 MHz (EST from measured instruction counts) | **Pass** |
| R3 | Do our flash writes obey the MSPM0 rules? | Yes: each 64-bit word is programmed once between erases. New issue found: erased flash reads are not deterministic and can raise an ECC reset | **Pass**, with one P6 design item |
| R4 | Can the bootloader start the watchdog and the app keep feeding it? | Yes (T1) | **Pass** |
| R5 | Is VTOR implemented? | Yes (T1) | **Pass** |
| R6 | SDK licensing and headless flash + GDB | Licensing fine (T1). Headless flash not proven: needs a board | **Open**, first step of P2 |

Recommendation: go ahead with P1 (board split). It needs no hardware and doesn't depend on R6. Close R6 at the start of P2 when a LaunchPad is on the desk.

## R1: bootloader size

`spike_size.c` links the bootloader's portable core (`fb_boot_run`, boot metadata, image verify, CRC-32C, Monocypher Ed25519 + SHA-512) with no vendor HAL, so the same code can be compared across CPUs. MEASURED, `.text` bytes:

| Core build | Cortex-M4 | Cortex-M0+ |
|---|---|---|
| Flags the STM32 build uses today (`-Os -O3 -fPIC`, see below) | 17,888 | 25,920 |
| `-Os` | 12,016 | 15,416 |

- The real STM32 bootloader is 24,500 B (text + data). Minus its core, the HAL, BSP, startup and libc part is about 6.6 KB.
- M0+ bootloader at `-Os`: 15.4 KB core plus an EST 3 to 7 KB TI DriverLib/BSP layer gives **18.4 to 22.6 KB of 28 KB**. At today's flags it would be about 32.5 KB and would not fit.
- **Build bug found:** the STM32 cross build is not compiled at `-Os`. CMake's Release default `-O3 -DNDEBUG` is appended after the toolchain's `-Os`, so `-O3` wins. `CMAKE_POSITION_INDEPENDENT_CODE ON` (needed for the host simulator's shared library) also adds `-fPIC` to the firmware. Fix both in P1. That frees about 5.9 KB on the M4 core alone.
- `-Os` costs almost no speed here: Ed25519 on M0+ is 22.5 M cycles at `-Os` vs 22.0 M at today's flags (MEASURED counts, EST cycles).

## R2: verify time

`bench_emu.py` runs the M0+ build of Monocypher in the Unicorn emulator (Cortex-M0 model, Armv6-M). It counts executed instructions exactly and turns them into cycles with the Cortex-M0+ per-instruction timings, assuming 0 flash wait states. The emulated code accepts the golden image's real signature (returns 0), so the right code path ran.

| Step (Cortex-M0+, `-Os`) | Instructions (MEASURED) | Cycles at 0 wait states (EST) | Time at 80 MHz (EST) | Peak stack (MEASURED) |
|---|---|---|---|---|
| `crypto_ed25519_check` on the 0x60-byte signed header | 15,163,923 | 22,476,313 | 0.28 s | 1,888 B |
| SHA-512 of a full 48,640-byte slot body | 8,174,017 | 12,237,677 | 0.15 s | 608 B |
| One full image verify (worst case, full slot) | | 34.7 M | **0.43 s** | |

- For comparison, the same Ed25519 check is 1,597,035 instructions on Cortex-M4 (MEASURED): the M0+ executes 9.5 times as many, mostly because it has no 32×32 to 64-bit multiply.
- The MSPM0G CPU has a single-cycle 32×32 multiply (T1, datasheet 8.1), so the 1-cycle `MULS` column applies. With a 32-cycle multiplier it would have been 78.5 M cycles.
- 80 MHz needs 2 flash wait states (T1). Prefetch and a 4-line I-cache hide part of that (T1). I bound the penalty at ×1.5 (EST): **0.43 to 0.65 s per verify**. If the bootloader stays on the 32 MHz reset clock, multiply by 2.5.
- **Code finding:** a normal boot verifies the chosen image twice. `fb_boot_decide` verifies it once, then `fb_boot_run` verifies it again only to read the version. Today that is 0.87 to 1.3 s per normal boot (EST). Returning the version from the first verify halves it. Worst case (first slot bad, second checked) after the fix is 0.87 to 1.3 s.
- The watchdog is armed after verification, so these times don't race the 2 s watchdog. They do lengthen the reboot gap the other node sees.
- Stack: the bootloader linker script reserves 0x600 (1,536 B), less than the measured 1,888 B for the Ed25519 check alone. RAM is not short (32 KB), so reserve at least 3 KB on M0+ and recheck the STM32 build the same way.

## R3: flash rules

T1 facts: 64-bit flash word (+8 ECC bits), word line = 16 words (128 B), sector = 8 word lines (1 KB), up to 83 program operations per word line before erase, word program 50 to 275 µs, sector erase 4 to 200 ms depending on wear, bank erase up to 220 ms. The first 32 KB of flash is rated for 100 k erase cycles (datasheet footnote on the memory map), the rest for 10 k.

- `boot_meta.c` erases the target sector and then writes the record once. `ota_receiver.c` erases each sector lazily and programs each 8-byte word once. TRM 6.3: if no 16-bit location is programmed more than once before an erase, the word-line limit "will never be reached". So both writers are compliant as written.
- The metadata copies at 0x7000 and 0x7800 fall inside the first 32 KB, the high-endurance region. Keep them there.
- Sector erase max 200 ms stays below the 500 ms peer timeout (D6). Whether the CPU stalls while the single bank erases is EST (assumed yes, as on the STM32) and must be measured.
- **New issue (T1, TRM 6.3.6):** "an erased location will return nondeterministic data when read". The ECC check on reads can raise an uncorrectable (DED) error, and by default a flash DED triggers a SYSRST (TRM SYSCTL register description). On a fresh board both metadata copies are erased, so the bootloader's first read could reset the chip in a loop.
  - The shared logic is already safe against garbage. Metadata needs magic + CRC, images need magic + signature, and nothing in `firmware/common` treats 0xFF as "erased".
  - The risk is only in the hardware read path. P6 design: `bsp_flash.c` reads metadata and slot headers through the ECC-uncorrected alias at 0x0040_0000 (T1, memory map) or checks the first word with BLANKVERIFY first. CRC and signature still catch bad data.
  - Optionally, set DED to raise an NMI instead of a reset during the bootloader.
  - Host-side follow-up for P1: give `tests/fake_flash.c` an option to return random bytes for erased pages, so the boot and OTA tests prove the logic tolerates it.
- Never write NONMAIN (the boot configuration sector). A bad NONMAIN can lock the device for good (TRM, NONMAIN and boot configuration sections).

## R4: watchdog across bootloader to app (T1, TRM chapter 40)

- WWDT0 is off after SYSRST. The first keyed write to WWDTCTL0 enables it, and from then on WWDTCTL0 is write-protected: any further write is a violation. Only a SYSRST stops it. Jumping to the app is not a reset, so a watchdog started by the bootloader keeps running in the app. This is what the design needs.
- 2.00 s is available exactly: PER = 0x3 (2^15 counts) with CLKDIV = /2 on the 32 kHz LFCLK.
- Set the closed window to 0%. Otherwise feeding too early (during the closed window) is itself a violation.
- The app feeds by writing 0xA7 to WWDTCNTRST. It must never write WWDTCTL0.
- Default WWDT0 violation is a BOOTRST, which reruns the ROM boot configuration and then our bootloader. That's the reset the trial-boot counter expects.
- By default the WWDT stops while the CPU is halted by the debugger, so GDB sessions don't reset the board.

## R5: vector table relocation (T1)

- The MSPM0G Cortex-M0+ implements VTOR ("Vector table offset support" in the CPU chapter; `SCB->VTOR` at 0xE000_ED08 in TRM table 3-6). The 0x200-byte image header keeps the app's vector table 512-byte aligned, which covers the Armv6-M alignment rule for this table size.
- `jump_to_app` must change for M0+: the NVIC has one ICER/ICPR register, not eight. Clearing `ICER[1..7]` would write reserved addresses. Fix in P6.

## R6: SDK and tooling

- TI MSPM0 SDK 2.11 manifest (T1): DriverLib and the device headers are BSD-3-Clause, CMSIS-Core is Apache-2.0, and FreeRTOS is MIT. All can be vendored as a pinned copy. The manifest doesn't name the GCC startup file or linker scripts. Check their file headers when vendoring, or write our own (we already write our own linker scripts).
- Headless flash and GDB over the on-board XDS110: not proven.
  - Homebrew has OpenOCD 0.12.0 but not UniFlash.
  - I found no confirmation that mainline OpenOCD 0.12.0 has an MSPM0 flash driver. Zephyr's `lp_mspm0g3507` board flashes through a specified OpenOCD source tree.
  - Candidates for the first P2 session, in order: TI UniFlash CLI (`dslite`), OpenOCD built from current source, pyOCD with the XDS110 in CMSIS-DAP mode.
- Check jumpers J101 (XDS110 backchannel UART on PA10/PA11, also the ROM BSL pins) when the board arrives.

## Flash map for P1 (from the T1 memory map; sizes EST until P6)

| Region | Address | Size |
|---|---|---|
| Bootloader | 0x0000_0000 | 28 KB |
| Metadata A | 0x0000_7000 | 1 KB (one sector) |
| Metadata B | 0x0000_7800 | 1 KB (one sector, with a 1 KB gap kept for symmetry with STM32) |
| Slot A | 0x0000_8000 | 48 KB |
| Slot B | 0x0001_4000 | 48 KB |
| SRAM (unchecked alias) | 0x2020_0000 | 32 KB |

VERIFY: the datasheet memory map labels the ECC-corrected flash "128KB-8B" with the range ending at 0x0001_FFF8. That most likely names the start of the last 8-byte word (so the full 128 KB is usable), but slot B ends exactly at 128 KB, so confirm in the TRM memory map before relying on the last word.

## Sources

- TI MSPM0G350x datasheet SLASEX6C (rev Oct 2025): https://www.ti.com/lit/gpn/mspm0g3507
- TI MSPM0 G-Series TRM SLAU846E: https://www.ti.com/lit/ug/slau846/slau846.pdf
- TI MSPM0 SDK 2.11 manifest: https://software-dl.ti.com/msp430/esd/MSPM0-SDK/2_11_00_07/manifest_mspm0_sdk_2_11_00_07.html
- Zephyr `lp_mspm0g3507` board commit (OpenOCD flashing): https://juju.nz/src/michaelh/zephyr/commit/dda92f8dd85b90dd28d9aadf1637bc8f044734c5
