#!/usr/bin/env python3
"""Enumerate MSVC x64 RTTI vftables from a PE image."""

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
    parser.add_argument("--slot", type=int, help="Only retain vftables containing this zero-based slot")
    parser.add_argument("--name", help="Case-insensitive regular expression matched against the RTTI name")
    parser.add_argument("--target", type=parse_address, action="append", default=[],
                        help="Only retain vftables containing this function VA; repeatable")
    parser.add_argument("--minimum-slots", type=int, default=1)
    parser.add_argument("--output", type=pathlib.Path)
    parser.add_argument("--slots-output", type=pathlib.Path,
                        help="Also write one row per slot for downstream function scans")
    args = parser.parse_args()
    if args.slot is not None and not 0 <= args.slot < 256:
        parser.error("slot must be between 0 and 255")
    if args.minimum_slots < 1:
        parser.error("minimum-slots must be positive")

    data = args.binary.read_bytes()
    image_base, section_list = sections(data)
    name_pattern = re.compile(args.name, re.IGNORECASE) if args.name else None

    def va_to_offset(va: int) -> int | None:
        rva = va - image_base
        section = next((item for item in section_list
                        if item["rva"] <= rva < item["rva"] + item["raw_size"]), None)
        if section is None:
            return None
        return section["raw_offset"] + rva - section["rva"]

    text_ranges = [
        (image_base + item["rva"], image_base + item["rva"] + item["raw_size"])
        for item in section_list if item["name"] == ".text"
    ]

    def is_code_pointer(value: int) -> bool:
        return any(start <= value < end for start, end in text_ranges)

    rows = []
    slot_rows = []
    seen_vftables: set[int] = set()
    for section in section_list:
        if section["name"] != ".rdata":
            continue
        raw = data[section["raw_offset"]:section["raw_offset"] + section["raw_size"]]
        for local_offset in range(0, max(0, len(raw) - 7), 8):
            locator_va = struct.unpack_from("<Q", raw, local_offset)[0]
            locator_offset = va_to_offset(locator_va)
            if locator_offset is None or locator_offset + 24 > len(data):
                continue
            signature, object_offset, constructor_offset, type_rva, hierarchy_rva, self_rva = \
                struct.unpack_from("<IIIIII", data, locator_offset)
            if signature != 1 or image_base + self_rva != locator_va:
                continue
            type_offset = va_to_offset(image_base + type_rva)
            if type_offset is None or type_offset + 17 > len(data):
                continue
            name_end = data.find(b"\0", type_offset + 16, min(len(data), type_offset + 1024))
            if name_end < 0:
                continue
            try:
                type_name = data[type_offset + 16:name_end].decode("ascii")
            except UnicodeDecodeError:
                continue
            if not type_name.startswith(".?A") or (name_pattern and not name_pattern.search(type_name)):
                continue

            locator_pointer_va = image_base + section["rva"] + local_offset
            vftable_va = locator_pointer_va + 8
            if vftable_va in seen_vftables:
                continue
            seen_vftables.add(vftable_va)
            vftable_offset = section["raw_offset"] + local_offset + 8
            slots = []
            for slot in range(256):
                offset = vftable_offset + slot * 8
                if offset + 8 > len(data):
                    break
                target = struct.unpack_from("<Q", data, offset)[0]
                if not is_code_pointer(target):
                    break
                slots.append(target)
            if len(slots) < args.minimum_slots or (args.slot is not None and len(slots) <= args.slot):
                continue
            matching_slots = [(slot, target) for slot, target in enumerate(slots)
                              if target in args.target]
            if args.target and not matching_slots:
                continue
            rows.append({
                "type_name": type_name,
                "vftable_va": f"0x{vftable_va:X}",
                "locator_va": f"0x{locator_va:X}",
                "object_offset": object_offset,
                "constructor_offset": constructor_offset,
                "hierarchy_va": f"0x{image_base + hierarchy_rva:X}",
                "slots": len(slots),
                "selected_slot": args.slot if args.slot is not None else "",
                "selected_target": f"0x{slots[args.slot]:X}" if args.slot is not None else "",
                "matching_slots": ";".join(f"{slot}:0x{target:X}" for slot, target in matching_slots),
            })
            slot_rows.extend({
                "type_name": type_name,
                "vftable_va": f"0x{vftable_va:X}",
                "slot": slot,
                "target": f"0x{target:X}",
            } for slot, target in enumerate(slots))

    rows.sort(key=lambda row: (row["type_name"], row["vftable_va"]))
    fields = ["type_name", "vftable_va", "locator_va", "object_offset", "constructor_offset",
              "hierarchy_va", "slots", "selected_slot", "selected_target", "matching_slots"]
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
    if args.slots_output:
        args.slots_output.parent.mkdir(parents=True, exist_ok=True)
        with args.slots_output.open("w", encoding="utf-8-sig", newline="") as stream:
            writer = csv.DictWriter(stream, fieldnames=["type_name", "vftable_va", "slot", "target"])
            writer.writeheader()
            writer.writerows(slot_rows)
    print(f"vftables={len(rows)}", file=sys.stderr)
    return 0 if rows else 1


if __name__ == "__main__":
    raise SystemExit(main())
