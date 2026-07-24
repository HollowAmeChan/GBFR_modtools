#!/usr/bin/env python3
"""Find decoded x86-64 branch and RIP-relative references in a PE image."""

from __future__ import annotations

import argparse
import csv
import pathlib
import re
import struct
import sys

from find_hash_constants import sections


def parse_address(value: str) -> int:
    return int(value, 0)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("binary", type=pathlib.Path)
    parser.add_argument("target", type=parse_address, nargs="+", help="Target VA, for example 0x1446EA570")
    parser.add_argument("--output", type=pathlib.Path)
    parser.add_argument("--pointers", action="store_true", help="Also scan for absolute VA and image-relative RVA pointers")
    parser.add_argument("--rip-relative", action="store_true",
                        help="Also find x64 RIP-relative memory references")
    args = parser.parse_args()

    data = args.binary.read_bytes()
    image_base, section_list = sections(data)
    targets = set(args.target)
    rows = []
    try:
        from capstone import CS_ARCH_X86, CS_MODE_64, Cs
    except ImportError as error:
        parser.error(f"x64 instruction scanning requires the capstone Python package: {error}")

    decoder = Cs(CS_ARCH_X86, CS_MODE_64)
    decoder.skipdata = True
    direct_target = re.compile(r"^0x[0-9a-f]+$")
    rip_operand = re.compile(r"\[rip\s*([+-])\s*(0x[0-9a-f]+)\]")
    for section in section_list:
        raw = data[section["raw_offset"]:section["raw_offset"] + section["raw_size"]]
        section_va = image_base + section["rva"]
        if section["name"] == ".text":
            executable = bytearray(raw)
            view = memoryview(executable)
            cursor = 0
            while cursor < len(view):
                next_cursor = cursor
                for address, size, mnemonic, operands in decoder.disasm_lite(
                        view[cursor:], section_va + cursor, count=250_000):
                    next_cursor = address + size - section_va
                    target_va = None
                    kind = None
                    if mnemonic in ("call", "jmp") and direct_target.fullmatch(operands):
                        target_va = int(operands, 16)
                        kind = mnemonic
                    elif args.rip_relative:
                        match = rip_operand.search(operands)
                        if match:
                            displacement = int(match.group(2), 16)
                            if match.group(1) == "-":
                                displacement = -displacement
                            target_va = address + size + displacement
                            kind = f"rip_relative_{mnemonic}"
                    if target_va not in targets:
                        continue
                    rows.append({
                        "target_va": f"0x{target_va:X}",
                        "source_va": f"0x{address:X}",
                        "instruction": kind,
                        "section": section["name"],
                        "file_offset": f"0x{section['raw_offset'] + address - section_va:X}",
                    })
                if next_cursor <= cursor:
                    break
                cursor = next_cursor

        if args.pointers:
            for target_va in targets:
                needles = [("absolute_qword", struct.pack("<Q", target_va))]
                target_rva = target_va - image_base
                if 0 <= target_rva <= 0xFFFFFFFF:
                    needles.append(("image_rva_dword", struct.pack("<I", target_rva)))
                for kind, needle in needles:
                    start = 0
                    while True:
                        offset = raw.find(needle, start)
                        if offset < 0:
                            break
                        rows.append({
                            "target_va": f"0x{target_va:X}",
                            "source_va": f"0x{section_va + offset:X}",
                            "instruction": kind,
                            "section": section["name"],
                            "file_offset": f"0x{section['raw_offset'] + offset:X}",
                        })
                        start = offset + 1

    rows.sort(key=lambda row: (row["target_va"], row["source_va"]))
    fields = ["target_va", "source_va", "instruction", "section", "file_offset"]
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
    print(f"targets={len(targets)} xrefs={len(rows)}", file=sys.stderr)
    return 0 if rows else 1


if __name__ == "__main__":
    raise SystemExit(main())
