"""P0 spike: run one bench function from an ARM ELF in the Unicorn emulator and count what it executes.

Unicorn is not cycle accurate. This script counts instructions exactly, then turns them into an ESTIMATED Cortex-M0+
cycle count using the per-class timings in the Arm Cortex-M0+ Technical Reference Manual (zero flash wait states).
Flash wait states and the multiplier option are reported as separate knobs, because they are not known yet (VERIFY).

Usage: python bench_emu.py <elf> <function> [--cpu m0|m4]
"""
import argparse
import sys

from elftools.elf.elffile import ELFFile
from unicorn import UC_ARCH_ARM, UC_MODE_MCLASS, UC_MODE_THUMB, UC_HOOK_CODE, Uc
from unicorn import arm_const as A

FLASH, FLASH_SIZE = 0x00000000, 0x20000
RAM, RAM_SIZE = 0x20200000, 0x8000
STOP = 0x0001FFF0  # return address planted in LR; emulation stops when execution reaches it


def classify(hw, nxt, addr, size):
    """Return (class, base cycles) for one Thumb instruction on Cortex-M0+ (TRM table 3-1)."""
    if size == 4:
        return "bl", 3
    if 0x4800 <= hw <= 0x4FFF or 0x5000 <= hw <= 0x9FFF:
        return "ldst", 2
    if 0xB400 <= hw <= 0xB5FF:
        return "push", 1 + bin(hw & 0x1FF).count("1")
    if 0xBC00 <= hw <= 0xBDFF:
        n = bin(hw & 0x1FF).count("1")
        return ("pop_pc", 3 + n) if hw & 0x100 else ("pop", 1 + n)
    if 0xC000 <= hw <= 0xCFFF:
        return "ldm_stm", 1 + bin(hw & 0xFF).count("1")
    if 0xD000 <= hw <= 0xDEFF:
        return ("b_taken", 2) if nxt != addr + 2 else ("b_not", 1)
    if 0xE000 <= hw <= 0xE7FF:
        return "b", 2
    if 0x4700 <= hw <= 0x47FF:
        return "bx", 2
    if 0x4340 <= hw <= 0x437F:
        return "muls", 1
    if (0x4400 <= hw <= 0x44FF or 0x4600 <= hw <= 0x46FF) and ((hw & 0x80) >> 4 | (hw & 7)) == 15:
        return "pc_write", 2
    return "alu", 1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("elf")
    ap.add_argument("function")
    ap.add_argument("--cpu", choices=["m0", "m4"], default="m0")
    a = ap.parse_args()

    uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
    uc.ctl_set_cpu_model(A.UC_CPU_ARM_CORTEX_M0 if a.cpu == "m0" else A.UC_CPU_ARM_CORTEX_M4)
    uc.mem_map(FLASH, FLASH_SIZE)
    uc.mem_map(RAM, RAM_SIZE)
    with open(a.elf, "rb") as f:
        elf = ELFFile(f)
        for seg in elf.iter_segments():
            if seg["p_type"] == "PT_LOAD" and seg["p_filesz"]:
                uc.mem_write(seg["p_paddr"], seg.data())
        fn = elf.get_section_by_name(".symtab").get_symbol_by_name(a.function)[0]["st_value"] & ~1

    counts, cycles, total = {}, 0, 0
    min_sp = RAM + RAM_SIZE
    pending = None  # (addr, size, halfword) of the previous instruction; its class needs the next PC

    def hook(_uc, addr, size, _ud):
        nonlocal pending, cycles, total, min_sp
        min_sp = min(min_sp, _uc.reg_read(A.UC_ARM_REG_SP))
        if pending:
            pa, ps, phw = pending
            cls, cyc = classify(phw, addr, pa, ps)
            counts[cls] = counts.get(cls, 0) + 1
            cycles += cyc
            total += 1
        hw = int.from_bytes(_uc.mem_read(addr, 2), "little")
        pending = (addr, size, hw)

    uc.hook_add(UC_HOOK_CODE, hook)
    uc.reg_write(A.UC_ARM_REG_SP, RAM + RAM_SIZE)
    uc.reg_write(A.UC_ARM_REG_LR, STOP | 1)
    uc.emu_start(fn | 1, STOP)
    if pending:
        cls, cyc = classify(pending[2], STOP, pending[0], pending[1])
        counts[cls] = counts.get(cls, 0) + 1
        cycles += cyc
        total += 1

    ret = uc.reg_read(A.UC_ARM_REG_R0)
    muls = counts.get("muls", 0)
    print(f"{a.function} ({a.cpu}) returned {ret}")
    print(f"  instructions executed : {total:,}")
    print(f"  peak stack (measured)  : {RAM + RAM_SIZE - min_sp:,} bytes")
    if a.cpu == "m0":
        print(f"  EST cycles, 0 wait states, 1-cycle MULS : {cycles:,}")
        print(f"  EST cycles, 0 wait states, 32-cycle MULS: {cycles + 31 * muls:,}")
        print("  classes: " + ", ".join(f"{k}={v:,}" for k, v in sorted(counts.items(), key=lambda kv: -kv[1])))
    return 0


if __name__ == "__main__":
    sys.exit(main())
