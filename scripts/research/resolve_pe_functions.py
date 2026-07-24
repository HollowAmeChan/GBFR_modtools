#!/usr/bin/env python3
"""Resolve x64 PE virtual addresses to .pdata runtime-function ranges."""

from __future__ import annotations

import argparse
import csv
import pathlib
import struct
import sys

from find_hash_constants import sections


def parse_address(value: str) -> int:
    return int(value, 0)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("binary", type=pathlib.Path)
    parser.add_argument("address", type=parse_address, nargs="+")
    parser.add_argument("--output", type=pathlib.Path)
    args = parser.parse_args()

    data = args.binary.read_bytes()
    image_base, section_list = sections(data)
    pdata = next((item for item in section_list if item["name"] == ".pdata"), None)
    if pdata is None:
        parser.error("PE image has no .pdata section")

    functions = []
    raw_end = pdata["raw_offset"] + pdata["raw_size"]
    for offset in range(pdata["raw_offset"], raw_end - 11, 12):
        begin_rva, end_rva, unwind_rva = struct.unpack_from("<III", data, offset)
        if not begin_rva or end_rva <= begin_rva:
            continue
        functions.append((image_base + begin_rva, image_base + end_rva, image_base + unwind_rva))
    functions.sort()

    rows = []
    for address in args.address:
        match = next((item for item in functions if item[0] <= address < item[1]), None)
        rows.append({
            "address": f"0x{address:X}",
            "function_start": f"0x{match[0]:X}" if match else "",
            "function_end": f"0x{match[1]:X}" if match else "",
            "function_size": match[1] - match[0] if match else "",
            "unwind_info": f"0x{match[2]:X}" if match else "",
        })

    fields = ["address", "function_start", "function_end", "function_size", "unwind_info"]
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
    return 0 if all(row["function_start"] for row in rows) else 1


if __name__ == "__main__":
    raise SystemExit(main())
