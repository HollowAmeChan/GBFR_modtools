#!/usr/bin/env python3
"""Find x86-64 PE instructions whose rendered text matches a regular expression."""

from __future__ import annotations

import argparse
import csv
import pathlib
import re
import sys

from find_hash_constants import sections


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("binary", type=pathlib.Path)
    parser.add_argument("pattern", help="Case-insensitive regular expression matched against 'mnemonic operands'")
    parser.add_argument("--output", type=pathlib.Path)
    args = parser.parse_args()

    try:
        from capstone import CS_ARCH_X86, CS_MODE_64, Cs
    except ImportError as error:
        parser.error(f"x64 instruction scanning requires the capstone Python package: {error}")

    pattern = re.compile(args.pattern, re.IGNORECASE)
    data = args.binary.read_bytes()
    image_base, section_list = sections(data)
    decoder = Cs(CS_ARCH_X86, CS_MODE_64)
    decoder.skipdata = True
    rows = []
    for section in section_list:
        if section["name"] != ".text":
            continue
        raw = data[section["raw_offset"]:section["raw_offset"] + section["raw_size"]]
        section_va = image_base + section["rva"]
        view = memoryview(bytearray(raw))
        cursor = 0
        while cursor < len(view):
            next_cursor = cursor
            for address, size, mnemonic, operands in decoder.disasm_lite(
                    view[cursor:], section_va + cursor, count=250_000):
                next_cursor = address + size - section_va
                rendered = f"{mnemonic} {operands}".rstrip()
                if pattern.search(rendered):
                    rows.append({
                        "source_va": f"0x{address:X}",
                        "instruction": rendered,
                        "section": section["name"],
                        "file_offset": f"0x{section['raw_offset'] + address - section_va:X}",
                    })
            if next_cursor <= cursor:
                break
            cursor = next_cursor

    fields = ["source_va", "instruction", "section", "file_offset"]
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
    print(f"matches={len(rows)}", file=sys.stderr)
    return 0 if rows else 1


if __name__ == "__main__":
    raise SystemExit(main())
