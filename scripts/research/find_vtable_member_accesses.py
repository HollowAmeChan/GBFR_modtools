#!/usr/bin/env python3
"""Find selected member displacements inside functions referenced by a vftable CSV."""

from __future__ import annotations

import argparse
import csv
import pathlib
import struct
import sys

from find_hash_constants import sections


def parse_integer(value: str) -> int:
    return int(value, 0)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("binary", type=pathlib.Path)
    parser.add_argument("slots", type=pathlib.Path,
                        help="CSV produced by enumerate_msvc_vtables.py --slots-output")
    parser.add_argument("--displacement", type=parse_integer, action="append", required=True)
    parser.add_argument("--maximum-bytes", type=parse_integer, default=0x800)
    parser.add_argument("--output", type=pathlib.Path)
    args = parser.parse_args()

    try:
        from capstone import CS_ARCH_X86, CS_MODE_64, CS_OP_MEM, Cs
    except ImportError as error:
        parser.error(f"x64 disassembly requires the capstone Python package: {error}")

    data = args.binary.read_bytes()
    image_base, section_list = sections(data)
    text = next((item for item in section_list if item["name"] == ".text"), None)
    if text is None:
        parser.error("PE image has no .text section")
    text_va = image_base + text["rva"]
    text_end = text_va + text["raw_size"]
    pdata = next((item for item in section_list if item["name"] == ".pdata"), None)
    if pdata is None:
        parser.error("PE image has no .pdata section")
    pdata_raw = data[pdata["raw_offset"]:pdata["raw_offset"] + pdata["raw_size"]]
    runtime_functions = sorted(
        (image_base + begin, image_base + end)
        for offset in range(0, len(pdata_raw) - 11, 12)
        for begin, end, _ in (struct.unpack_from("<III", pdata_raw, offset),)
        if begin < end
    )

    def function_range(address: int) -> tuple[int, int] | None:
        return next((item for item in runtime_functions if item[0] <= address < item[1]), None)

    with args.slots.open(encoding="utf-8-sig", newline="") as stream:
        slot_rows = list(csv.DictReader(stream))
    target_slots: dict[int, list[str]] = {}
    for row in slot_rows:
        target_slots.setdefault(int(row["target"], 0), []).append(row["slot"])

    decoder = Cs(CS_ARCH_X86, CS_MODE_64)
    decoder.detail = True
    decoder.skipdata = True
    wanted = set(args.displacement)
    rows = []
    for target, slots in sorted(target_slots.items()):
        if not text_va <= target < text_end:
            continue
        bounds = function_range(target)
        scan_end = bounds[1] if bounds else min(target + args.maximum_bytes, text_end)
        offset = text["raw_offset"] + target - text_va
        raw = data[offset:offset + scan_end - target]
        saw_instruction = False
        for instruction in decoder.disasm(raw, target):
            if instruction.id == 0:
                continue
            if bounds is None and saw_instruction and instruction.mnemonic == "int3":
                break
            saw_instruction = True
            displacements = set()
            for operand in instruction.operands:
                if operand.type != CS_OP_MEM or not operand.mem.base or operand.mem.disp not in wanted:
                    continue
                if instruction.reg_name(operand.mem.base) in ("rsp", "rbp"):
                    continue
                displacements.add(operand.mem.disp)
            displacements = sorted(displacements)
            for displacement in displacements:
                rows.append({
                    "slots": ";".join(slots),
                    "function_va": f"0x{target:X}",
                    "function_end": f"0x{bounds[1]:X}" if bounds else "",
                    "boundary_source": "pdata" if bounds else "int3_fallback",
                    "instruction_va": f"0x{instruction.address:X}",
                    "displacement": f"0x{displacement:X}",
                    "instruction": f"{instruction.mnemonic} {instruction.op_str}".rstrip(),
                })

    fields = ["slots", "function_va", "function_end", "boundary_source",
              "instruction_va", "displacement", "instruction"]
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        with args.output.open("w", encoding="utf-8-sig", newline="") as stream:
            writer = csv.DictWriter(stream, fieldnames=fields)
            writer.writeheader()
            writer.writerows(rows)
    else:
        writer = csv.DictWriter(sys.stdout, fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)
    print(f"functions={len(target_slots)} accesses={len(rows)}", file=sys.stderr)
    return 0 if rows else 1


if __name__ == "__main__":
    raise SystemExit(main())
