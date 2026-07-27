#!/usr/bin/env python3
"""Annotate RIP-relative ASCII string references in an x64 PE code range."""

from __future__ import annotations

import argparse
import csv
import pathlib
import string
import sys

from capstone import Cs, CS_ARCH_X86, CS_MODE_64
from capstone.x86 import X86_OP_MEM, X86_REG_RIP

from find_hash_constants import sections


def va_to_offset(va: int, image_base: int, section_list: list[dict]) -> int | None:
    rva = va - image_base
    for section in section_list:
        delta = rva - section["rva"]
        if 0 <= delta < section["raw_size"]:
            return section["raw_offset"] + delta
    return None


def ascii_string(data: bytes, offset: int | None, limit: int = 160) -> str | None:
    if offset is None or offset < 0 or offset >= len(data):
        return None
    end = data.find(b"\0", offset, min(len(data), offset + limit))
    if end < 0 or end == offset:
        return None
    raw = data[offset:end]
    try:
        value = raw.decode("ascii")
    except UnicodeDecodeError:
        return None
    return value if all(character in string.printable and character not in "\r\n\t" for character in value) else None


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=pathlib.Path)
    parser.add_argument("start", type=lambda value: int(value, 0), help="Function start VA")
    parser.add_argument("end", type=lambda value: int(value, 0), help="Function end VA (exclusive)")
    parser.add_argument("--output", type=pathlib.Path)
    args = parser.parse_args()

    data = args.binary.read_bytes()
    image_base, section_list = sections(data)
    start_offset = va_to_offset(args.start, image_base, section_list)
    end_offset = va_to_offset(args.end - 1, image_base, section_list)
    if start_offset is None or end_offset is None:
        parser.error("code range is not backed by PE section data")

    decoder = Cs(CS_ARCH_X86, CS_MODE_64)
    decoder.detail = True
    rows = []
    code = data[start_offset:end_offset + 1]
    for instruction in decoder.disasm(code, args.start):
        for operand in instruction.operands:
            if operand.type != X86_OP_MEM or operand.mem.base != X86_REG_RIP:
                continue
            target = instruction.address + instruction.size + operand.mem.disp
            value = ascii_string(data, va_to_offset(target, image_base, section_list))
            if value is not None:
                rows.append({
                    "instruction_va": f"0x{instruction.address:X}",
                    "instruction": f"{instruction.mnemonic} {instruction.op_str}",
                    "target_va": f"0x{target:X}",
                    "text": value,
                })

    fields = ["instruction_va", "instruction", "target_va", "text"]
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
    print(f"rip_strings={len(rows)}", file=sys.stderr)
    return 0 if rows else 1


if __name__ == "__main__":
    raise SystemExit(main())
