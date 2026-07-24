#!/usr/bin/env python3
"""Find nearby writes to two member displacements through the same base register."""

from __future__ import annotations

import argparse
import bisect
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
    parser.add_argument("first", type=parse_integer)
    parser.add_argument("second", type=parse_integer)
    parser.add_argument("--window", type=parse_integer, default=0x200)
    parser.add_argument("--output", type=pathlib.Path)
    args = parser.parse_args()

    try:
        from capstone import CS_ARCH_X86, CS_MODE_64, CS_OP_MEM, Cs
    except ImportError as error:
        parser.error(f"x64 instruction scanning requires the capstone Python package: {error}")

    data = args.binary.read_bytes()
    image_base, section_list = sections(data)
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
    function_starts = [item[0] for item in runtime_functions]

    def function_range(address: int) -> tuple[int, int] | None:
        index = bisect.bisect_right(function_starts, address) - 1
        if index < 0:
            return None
        bounds = runtime_functions[index]
        return bounds if address < bounds[1] else None

    decoder = Cs(CS_ARCH_X86, CS_MODE_64)
    decoder.detail = True
    decoder.skipdata = True
    wanted = {args.first, args.second}
    writes: dict[tuple[int, int, str], list[dict[str, object]]] = {}
    for section in section_list:
        if section["name"] != ".text":
            continue
        raw = data[section["raw_offset"]:section["raw_offset"] + section["raw_size"]]
        section_va = image_base + section["rva"]
        view = memoryview(bytearray(raw))
        cursor = 0
        while cursor < len(view):
            next_cursor = cursor
            for instruction in decoder.disasm(view[cursor:], section_va + cursor, count=250_000):
                next_cursor = instruction.address + instruction.size - section_va
                if instruction.id == 0:
                    continue
                if not instruction.operands or instruction.operands[0].type != CS_OP_MEM:
                    continue
                memory = instruction.operands[0].mem
                if memory.disp not in wanted or not memory.base:
                    continue
                bounds = function_range(instruction.address)
                if bounds is None:
                    continue
                base = instruction.reg_name(memory.base)
                writes.setdefault((bounds[0], bounds[1], base), []).append({
                    "address": instruction.address,
                    "field": memory.disp,
                    "instruction": f"{instruction.mnemonic} {instruction.op_str}".rstrip(),
                })
            if next_cursor <= cursor:
                break
            cursor = next_cursor

    rows = []
    seen: set[tuple[int, int]] = set()
    for (function_start, function_end, base), accesses in writes.items():
        accesses.sort(key=lambda item: int(item["address"]))
        left = 0
        for right, current in enumerate(accesses):
            current_address = int(current["address"])
            while int(accesses[left]["address"]) < current_address - args.window:
                left += 1
            for previous in accesses[left:right]:
                if previous["field"] == current["field"]:
                    continue
                key = (int(previous["address"]), current_address)
                if key in seen:
                    continue
                seen.add(key)
                rows.append({
                    "function_start": f"0x{function_start:X}",
                    "function_end": f"0x{function_end:X}",
                    "base_register": base,
                    "distance": current_address - int(previous["address"]),
                    "first_va": f"0x{int(previous['address']):X}",
                    "first_field": f"0x{int(previous['field']):X}",
                    "first_instruction": previous["instruction"],
                    "second_va": f"0x{current_address:X}",
                    "second_field": f"0x{int(current['field']):X}",
                    "second_instruction": current["instruction"],
                })

    rows.sort(key=lambda row: (row["distance"], row["first_va"], row["second_va"]))
    fields = ["function_start", "function_end", "base_register", "distance",
              "first_va", "first_field", "first_instruction",
              "second_va", "second_field", "second_instruction"]
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
    print(f"pairs={len(rows)}", file=sys.stderr)
    return 0 if rows else 1


if __name__ == "__main__":
    raise SystemExit(main())
