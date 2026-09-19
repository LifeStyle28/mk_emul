"""Assemble a tiny Thumb blinky for STM32F407 (GPIOD 12-15)."""

from pathlib import Path

base = 0x08000000


def u16(x: int) -> bytes:
    return int(x).to_bytes(2, "little")


def u32(x: int) -> bytes:
    return int(x).to_bytes(4, "little")


def ldr_lit(rt: int, insn_addr: int, lit_addr: int) -> int:
    pc = (insn_addr + 4) & ~3
    imm = (lit_addr - pc) // 4
    assert 0 <= imm <= 255, (hex(insn_addr), hex(lit_addr), imm)
    return 0x4800 | (rt << 8) | imm


def main() -> None:
    # Layout: vector (8 bytes) + code then 4-byte aligned literals.
    # We'll two-pass: emit instruction records with literal names.
    code_start = base + 8

    # Instruction list as ('op', args)
    # ops: ldr_lit rt name, movs rd imm, str rt rn, subs rd imm, bne label, b label, label
    program = [
        ("ldr_lit", 0, "RCC_AHB1ENR"),
        ("movs", 1, 8),
        ("str", 1, 0),
        ("ldr_lit", 0, "GPIOD_MODER"),
        ("ldr_lit", 1, "MODER_VAL"),
        ("str", 1, 0),
        ("label", "loop"),
        ("ldr_lit", 0, "GPIOD_BSRR"),
        ("ldr_lit", 1, "SET_VAL"),
        ("str", 1, 0),
        ("ldr_lit", 2, "DELAY"),
        ("label", "d1"),
        ("subs", 2, 1),
        ("bne", "d1"),
        ("ldr_lit", 1, "RST_VAL"),
        ("str", 1, 0),
        ("ldr_lit", 2, "DELAY"),
        ("label", "d2"),
        ("subs", 2, 1),
        ("bne", "d2"),
        ("b", "loop"),
    ]

    # First pass: addresses
    addr = code_start
    labels = {}
    insn_addrs = []
    for item in program:
        if item[0] == "label":
            labels[item[1]] = addr
            insn_addrs.append(None)
        else:
            insn_addrs.append(addr)
            addr += 2
    if addr % 4:
        addr += 2  # pad
    lit_names = ["RCC_AHB1ENR", "GPIOD_MODER", "MODER_VAL", "GPIOD_BSRR", "SET_VAL", "DELAY", "RST_VAL"]
    lit_values = {
        "RCC_AHB1ENR": 0x40023830,
        "GPIOD_MODER": 0x40020C00,
        "MODER_VAL": 0x55000000,
        "GPIOD_BSRR": 0x40020C18,
        "SET_VAL": 0x0000F000,
        "DELAY": 0x00020000,
        "RST_VAL": 0xF0000000,
    }
    lits = {}
    for name in lit_names:
        lits[name] = addr
        addr += 4

    halfs = []
    for item, ia in zip(program, insn_addrs):
        op = item[0]
        if op == "label":
            continue
        if op == "ldr_lit":
            halfs.append(ldr_lit(item[1], ia, lits[item[2]]))
        elif op == "movs":
            halfs.append(0x2000 | (item[1] << 8) | item[2])
        elif op == "str":
            rt, rn = item[1], item[2]
            halfs.append(0x6000 | (rn << 3) | rt)
        elif op == "subs":
            halfs.append(0x3800 | (item[1] << 8) | item[2])
        elif op == "bne":
            target = labels[item[1]]
            # imm8 = (target - (pc+4)) / 2, signed
            imm = (target - (ia + 4)) // 2
            assert -128 <= imm <= 127, imm
            halfs.append(0xD100 | (imm & 0xFF))
        elif op == "b":
            target = labels[item[1]]
            imm = (target - (ia + 4)) // 2
            halfs.append(0xE000 | (imm & 0x7FF))
        else:
            raise SystemExit(op)

    blob = bytearray()
    blob += u32(0x20020000)
    blob += u32(code_start | 1)
    for h in halfs:
        blob += u16(h)
    while len(blob) % 4:
        blob += u16(0)
    for name in lit_names:
        blob += u32(lit_values[name])

    out_cpp = Path(__file__).resolve().parents[1] / "src" / "core" / "BuiltinFirmware.inc"
    lines = [", ".join(f"0x{b:02X}" for b in blob[i : i + 12]) for i in range(0, len(blob), 12)]
    out_cpp.write_text(",\n".join(lines) + "\n", encoding="ascii")
    bin_path = Path(__file__).resolve().parents[1] / "examples" / "blinky" / "builtin.bin"
    bin_path.parent.mkdir(parents=True, exist_ok=True)
    bin_path.write_bytes(blob)
    print(f"wrote {len(blob)} bytes to {out_cpp} and {bin_path}")


if __name__ == "__main__":
    main()
