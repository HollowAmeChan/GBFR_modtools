#!/usr/bin/env python3
"""Map MMAT hash constants into the game's shader-family dispatch functions."""

from __future__ import annotations

import argparse
import csv
import json
import pathlib
import struct

from find_hash_constants import sections


def parse_address(value: str) -> int:
    return int(value, 0)


def parse_table(value: str) -> tuple[str, int]:
    try:
        name, address = value.split("=", 1)
        return name, parse_address(address)
    except ValueError as error:
        raise argparse.ArgumentTypeError("table must be NAME=VA") from error


def parse_expectation(value: str) -> tuple[int, int]:
    try:
        hash_text, type_text = value.split(":", 1)
        return parse_address(hash_text), int(type_text, 0)
    except ValueError as error:
        raise argparse.ArgumentTypeError("expectation must be HASH:SHADER_TYPE") from error


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("binary", type=pathlib.Path)
    parser.add_argument("hash_occurrences", type=pathlib.Path,
                        help="CSV from find_hash_constants.py")
    parser.add_argument("--table", type=parse_table, action="append", required=True,
                        help="Dispatch table as NAME=VA; repeat for every stage")
    parser.add_argument("--count", type=int, default=30)
    parser.add_argument("--focus-hash", type=parse_address, action="append", default=[])
    parser.add_argument("--expect", type=parse_expectation, action="append", default=[],
                        help="Assert a direct owner as HASH:SHADER_TYPE; repeatable")
    parser.add_argument("--output", type=pathlib.Path, required=True)
    args = parser.parse_args()
    if args.count <= 0:
        parser.error("count must be positive")

    data = args.binary.read_bytes()
    image_base, section_list = sections(data)

    def va_to_offset(va: int) -> int | None:
        rva = va - image_base
        section = next((item for item in section_list
                        if item["rva"] <= rva < item["rva"] + item["raw_size"]), None)
        if section is None:
            return None
        return section["raw_offset"] + rva - section["rva"]

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

    occurrences: list[tuple[int, int]] = []
    with args.hash_occurrences.open("r", encoding="utf-8-sig", newline="") as stream:
        for row in csv.DictReader(stream):
            if row.get("section") == ".text" and row.get("va"):
                occurrences.append((parse_address(row["hash"]), parse_address(row["va"])))

    types = [{"shader_type": shader_type, "stages": {}} for shader_type in range(args.count)]
    owners: dict[int, list[dict]] = {}
    table_results = []
    for stage_name, table_va in args.table:
        table_offset = va_to_offset(table_va)
        if table_offset is None or table_offset + args.count * 8 > len(data):
            parser.error(f"table {stage_name} is outside file-backed PE data")
        entries = []
        for shader_type in range(args.count):
            target = struct.unpack_from("<Q", data, table_offset + shader_type * 8)[0]
            bounds = function_range(target)
            direct_hashes = sorted({hash_value for hash_value, va in occurrences
                                    if bounds and bounds[0] <= va < bounds[1]})
            stage = {
                "target": f"0x{target:X}",
                "function_start": f"0x{bounds[0]:X}" if bounds else None,
                "function_end": f"0x{bounds[1]:X}" if bounds else None,
                "direct_hashes": [f"0x{value:08X}" for value in direct_hashes],
            }
            types[shader_type]["stages"][stage_name] = stage
            entries.append(stage)
            for hash_value in direct_hashes:
                owners.setdefault(hash_value, []).append({
                    "shader_type": shader_type,
                    "stage": stage_name,
                    "function_start": stage["function_start"],
                })
        table_results.append({"name": stage_name, "va": f"0x{table_va:X}", "entries": entries})

    expectation_results = []
    failed = False
    for hash_value, shader_type in args.expect:
        actual_types = sorted({item["shader_type"] for item in owners.get(hash_value, [])})
        passed = shader_type in actual_types
        failed |= not passed
        expectation_results.append({
            "hash": f"0x{hash_value:08X}",
            "expected_shader_type": shader_type,
            "actual_shader_types": actual_types,
            "passed": passed,
        })

    focus = []
    for hash_value in args.focus_hash:
        raw_occurrences = [f"0x{va:X}" for candidate, va in occurrences if candidate == hash_value]
        focus.append({
            "hash": f"0x{hash_value:08X}",
            "exe_occurrences": raw_occurrences,
            "direct_dispatch_owners": owners.get(hash_value, []),
        })

    document = {
        "binary": str(args.binary),
        "image_base": f"0x{image_base:X}",
        "shader_type_count": args.count,
        "tables": [{"name": item["name"], "va": item["va"]} for item in table_results],
        "types": types,
        "focus": focus,
        "expectations": expectation_results,
        "summary": {
            "hash_occurrences": len(occurrences),
            "directly_owned_hashes": len(owners),
            "expectations_passed": sum(item["passed"] for item in expectation_results),
            "expectations_total": len(expectation_results),
        },
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(document, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(document["summary"], ensure_ascii=False, separators=(",", ":")))
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
