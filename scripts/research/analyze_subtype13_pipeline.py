#!/usr/bin/env python3
"""Correlate MMAT 0x8B8038FC samples with the executable pipeline-state table."""

from __future__ import annotations

import argparse
import collections
import json
import pathlib
import struct


PARAMETER_HASH = "0x8B8038FC"
DEFAULT_TABLE_VA = 0x146138780
RECORD_SIZE = 24
FAMILIES = {3: "Face", 4: "Hair", 5: "Metal", 6: "Skin"}
SOURCE_RECORDS = {"Face": 9, "Hair": 10, "Metal": 7, "Skin": 9}
TARGET_RECORD = 13


def pe_layout(data: bytes) -> tuple[int, list[dict]]:
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    if data[pe:pe + 4] != b"PE\0\0":
        raise ValueError("not a PE image")
    section_count = struct.unpack_from("<H", data, pe + 6)[0]
    optional_size = struct.unpack_from("<H", data, pe + 20)[0]
    optional = pe + 24
    magic = struct.unpack_from("<H", data, optional)[0]
    image_base = struct.unpack_from(
        "<Q" if magic == 0x20B else "<I", data, optional + (24 if magic == 0x20B else 28)
    )[0]
    table = optional + optional_size
    sections = []
    for index in range(section_count):
        offset = table + index * 40
        name = data[offset:offset + 8].split(b"\0", 1)[0].decode("ascii", errors="replace")
        virtual_size, rva, raw_size, raw_offset = struct.unpack_from("<IIII", data, offset + 8)
        sections.append({
            "name": name,
            "rva": rva,
            "virtual_size": virtual_size,
            "raw_size": raw_size,
            "raw_offset": raw_offset,
        })
    return image_base, sections


def va_to_offset(va: int, image_base: int, sections: list[dict]) -> int:
    rva = va - image_base
    section = next((item for item in sections
                    if item["rva"] <= rva < item["rva"] + item["virtual_size"]), None)
    if not section:
        raise ValueError(f"VA 0x{va:X} is outside PE sections")
    delta = rva - section["rva"]
    if delta >= section["raw_size"]:
        raise ValueError(f"VA 0x{va:X} is in an uninitialized section tail")
    return section["raw_offset"] + delta


def parameter_value(record: dict) -> bool | None:
    parameter = next((item for item in record.get("params", [])
                      if item.get("hash") == PARAMETER_HASH), None)
    return None if parameter is None else bool(parameter.get("raw", 0))


def counter_rows(counter: collections.Counter) -> list[dict]:
    return [{"value": value, "count": count} for value, count in counter.most_common()]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--materials", type=pathlib.Path, required=True)
    parser.add_argument("--binary", type=pathlib.Path, required=True)
    parser.add_argument("--table-va", type=lambda value: int(value, 0), default=DEFAULT_TABLE_VA)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    args = parser.parse_args()

    present = enabled = 0
    families: collections.Counter[str] = collections.Counter()
    categories: collections.Counter[str] = collections.Counter()
    shaders: collections.Counter[str] = collections.Counter()
    states: collections.Counter[str] = collections.Counter()
    files: set[str] = set()
    samples = []
    with args.materials.open("r", encoding="utf-8") as stream:
        for line in stream:
            record = json.loads(line)
            value = parameter_value(record)
            if value is None:
                continue
            present += 1
            if not value:
                continue
            enabled += 1
            shader_type = int(record["shader"].split("/", 1)[0])
            family = FAMILIES.get(shader_type, f"type {shader_type}")
            families[family] += 1
            categories[record["category"]] += 1
            shaders[record["shader"]] += 1
            states[
                f"shadow={record['shadow']} bool9={int(record['bool9'])} "
                f"bool10={int(record['bool10'])} ignore_alpha={int(record['ignore_alpha'])} "
                f"bool12={int(record['bool12'])}"
            ] += 1
            files.add(record["file"])
            samples.append({
                "file": record["file"],
                "material": record["material"],
                "material_hash": record["material_hash"],
                "family": family,
                "shader": record["shader"],
                "shadow": record["shadow"],
                "ignore_alpha": record["ignore_alpha"],
                "albedo": next((item["texture"] for item in record.get("maps", [])
                                if item["hash"] == "0x3F2B4D59"), ""),
            })

    binary = args.binary.read_bytes()
    image_base, sections = pe_layout(binary)
    table_offset = va_to_offset(args.table_va, image_base, sections)
    indices = sorted(set(SOURCE_RECORDS.values()) | {TARGET_RECORD}
                     | {value + 16 for value in SOURCE_RECORDS.values()} | {TARGET_RECORD + 16})
    records = {}
    for index in indices:
        record = binary[table_offset + index * RECORD_SIZE:table_offset + (index + 1) * RECORD_SIZE]
        if len(record) != RECORD_SIZE:
            raise ValueError(f"pipeline record {index} is truncated")
        records[index] = record

    transitions = []
    for family, source_index in SOURCE_RECORDS.items():
        source = records[source_index]
        target = records[TARGET_RECORD]
        changed = [offset for offset, (left, right) in enumerate(zip(source, target)) if left != right]
        transitions.append({
            "family": family,
            "table_block": "base",
            "source_record": source_index,
            "target_record": TARGET_RECORD,
            "changed_offsets": changed,
            "source_byte_20": f"0x{source[20]:02X}",
            "target_byte_20": f"0x{target[20]:02X}",
        })
        if changed != [20]:
            raise ValueError(f"unexpected {family} table transition: changed offsets {changed}")
        high_source = records[source_index + 16]
        high_target = records[TARGET_RECORD + 16]
        high_changed = [offset for offset, (left, right) in enumerate(zip(high_source, high_target))
                        if left != right]
        if high_changed != [20] or high_target != target:
            raise ValueError(f"high-block {family} transition does not mirror the base block")
        transitions.append({
            "family": family,
            "table_block": "high",
            "source_record": source_index + 16,
            "target_record": TARGET_RECORD + 16,
            "changed_offsets": high_changed,
            "source_byte_20": f"0x{high_source[20]:02X}",
            "target_byte_20": f"0x{high_target[20]:02X}",
        })

    if records[TARGET_RECORD][20] != 0x84:
        raise ValueError(
            f"current executable table contract changed: record 13 byte 20 is "
            f"0x{records[TARGET_RECORD][20]:02X}, expected 0x84"
        )
    if any(sample["family"] not in FAMILIES.values() for sample in samples):
        raise ValueError("enabled sample exists outside Face/Hair/Metal/Skin")

    report = {
        "parameter": PARAMETER_HASH,
        "materials_present": present,
        "materials_enabled": enabled,
        "enabled_files": sorted(files),
        "enabled_file_count": len(files),
        "enabled_families": counter_rows(families),
        "enabled_categories": counter_rows(categories),
        "enabled_shaders": counter_rows(shaders),
        "enabled_render_states": counter_rows(states),
        "pipeline_table": {
            "va": f"0x{args.table_va:X}",
            "record_size": RECORD_SIZE,
            "target_record": TARGET_RECORD,
            "target_record_hex": records[TARGET_RECORD].hex(),
            "transitions": transitions,
            "conclusion": "all four character families converge on record 13/29",
        },
        "samples": samples,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({
        "present": present,
        "enabled": enabled,
        "files": len(files),
        "families": dict(families),
        "target_record": TARGET_RECORD,
        "target_byte_20": f"0x{records[TARGET_RECORD][20]:02X}",
        "output": str(args.output),
    }, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
