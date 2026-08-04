#!/usr/bin/env python3
"""Correlate SOP targets with decoded CLP simulation nodes."""

from __future__ import annotations

import argparse
import csv
import json
import struct
from collections import Counter, defaultdict
from pathlib import Path
from typing import Any


SOP_VERSION = 0x20200309
TARGET_PROPERTY = 0x5B0292DD
SOURCE_PROPERTY = 0x1B5B0525
SENTINEL = 4095


def load_sop(path: Path) -> list[dict[str, Any]]:
    data = path.read_bytes()
    if len(data) < 12 or data[:4] != b"sop\0":
        raise ValueError(f"invalid SOP: {path}")
    version, count = struct.unpack_from("<II", data, 4)
    if version != SOP_VERSION:
        raise ValueError(f"unsupported SOP version 0x{version:08X}: {path}")
    table_end = 12 + count * 4
    if table_end > len(data):
        raise ValueError(f"SOP offset table outside file: {path}")
    offsets = struct.unpack_from(f"<{count}I", data, 12) if count else ()
    operations = []
    for index, begin in enumerate(offsets):
        end = offsets[index + 1] if index + 1 < count else len(data)
        if begin < table_end or end < begin or end - begin < 24 or (end - begin - 24) % 12:
            raise ValueError(f"invalid SOP operation {index}: {path}")
        type_hash, metadata, target_key, target, source_key, source = struct.unpack_from(
            "<6I", data, begin,
        )
        if target_key != TARGET_PROPERTY or source_key != SOURCE_PROPERTY:
            raise ValueError(f"invalid SOP target/source fields at operation {index}: {path}")
        property_count = (metadata >> 16) & 0xFF
        if property_count != (end - begin - 24) // 12:
            raise ValueError(f"SOP property count mismatch at operation {index}: {path}")
        properties = []
        for property_index in range(property_count):
            prop_hash, value_type, raw_value = struct.unpack_from(
                "<III", data, begin + 24 + property_index * 12,
            )
            value = struct.unpack("<f", struct.pack("<I", raw_value))[0] if value_type == 1 else raw_value
            properties.append({
                "hash": f"0x{prop_hash:08X}",
                "type": "float" if value_type == 1 else "integer",
                "value": value,
                "raw": f"0x{raw_value:08X}",
            })
        operations.append({
            "index": index,
            "type_hash": type_hash,
            "metadata": metadata,
            "target": target,
            "source": source,
            "properties": properties,
        })
    return operations


def model_from_sop(path: Path) -> str:
    return path.stem.casefold()


def load_clp_nodes(path: Path) -> dict[str, list[dict[str, Any]]]:
    records = json.loads(path.read_text(encoding="utf-8"))
    by_model: dict[str, list[dict[str, Any]]] = defaultdict(list)
    for record in records:
        model = str(record["model"]).casefold()
        ids = {int(node["no"]) for node in record["nodes"]}
        depths = {int(key): int(value) for key, value in record["topology"]["depths"].items()}
        for node in record["nodes"]:
            node_id = int(node["no"])
            by_model[model].append({
                "group": int(record["group"]),
                "node": node_id,
                "is_root": int(node.get("noUp", SENTINEL)) not in ids,
                "depth": depths.get(node_id),
                "no_up": int(node.get("noUp", SENTINEL)),
                "no_down": int(node.get("noDown", SENTINEL)),
            })
    return by_model


def analyze(data_root: Path, clp_json: Path) -> tuple[list[dict[str, Any]], dict[str, Any]]:
    clp_by_model = load_clp_nodes(clp_json)
    overlaps = []
    type_totals: Counter[int] = Counter()
    model_totals: Counter[str] = Counter()
    type_models: dict[int, set[str]] = defaultdict(set)
    overlap_models: dict[int, set[str]] = defaultdict(set)
    overlap_operations: Counter[int] = Counter()
    overlap_roots: Counter[int] = Counter()
    overlap_targets: dict[int, set[tuple[str, int]]] = defaultdict(set)
    sop_paths = sorted((data_root / "model" / "pl").glob("pl*/pl*.sop"))
    for sop_path in sop_paths:
        model = model_from_sop(sop_path)
        memberships: dict[int, list[dict[str, Any]]] = defaultdict(list)
        for node in clp_by_model.get(model, []):
            memberships[node["node"]].append(node)
        for operation in load_sop(sop_path):
            type_hash = int(operation["type_hash"])
            type_totals[type_hash] += 1
            model_totals[model] += 1
            type_models[type_hash].add(model)
            matches = memberships.get(int(operation["target"]), [])
            if matches:
                overlap_operations[type_hash] += 1
                overlap_models[type_hash].add(model)
                overlap_targets[type_hash].add((model, int(operation["target"])))
            for match in matches:
                overlap_roots[type_hash] += bool(match["is_root"])
                overlaps.append({
                    "model": model,
                    "group": match["group"],
                    "operation_index": operation["index"],
                    "type_hash": f"0x{type_hash:08X}",
                    "target": f"0x{operation['target']:03X}",
                    "source": f"0x{operation['source']:03X}",
                    "is_clp_root": match["is_root"],
                    "clp_depth": match["depth"],
                    "no_up": f"0x{match['no_up']:03X}",
                    "no_down": f"0x{match['no_down']:03X}",
                    "metadata": f"0x{operation['metadata']:08X}",
                    "properties": operation["properties"],
                })
    all_types = sorted(type_totals)
    summary = {
        "sop_models": len(model_totals),
        "sop_operations": sum(type_totals.values()),
        "clp_models": len(clp_by_model),
        "overlap_rows": len(overlaps),
        "overlap_operations": sum(overlap_operations.values()),
        "overlap_targets": len({(row["model"], row["target"]) for row in overlaps}),
        "types": [{
            "type_hash": f"0x{type_hash:08X}",
            "operations": type_totals[type_hash],
            "models": len(type_models[type_hash]),
            "clp_operations": overlap_operations[type_hash],
            "clp_models": len(overlap_models[type_hash]),
            "clp_targets": len(overlap_targets[type_hash]),
            "clp_root_rows": overlap_roots[type_hash],
        } for type_hash in all_types],
    }
    return overlaps, summary


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("data_root", type=Path, help="Extracted GBFR data directory")
    parser.add_argument("clp_json", type=Path, help="all_clp.json from analyze_clp.py")
    parser.add_argument("--json", type=Path, required=True)
    parser.add_argument("--csv", type=Path, required=True)
    parser.add_argument("--summary-json", type=Path, required=True)
    args = parser.parse_args()
    overlaps, summary = analyze(args.data_root, args.clp_json)
    args.json.parent.mkdir(parents=True, exist_ok=True)
    args.csv.parent.mkdir(parents=True, exist_ok=True)
    args.summary_json.parent.mkdir(parents=True, exist_ok=True)
    args.json.write_text(json.dumps(overlaps, ensure_ascii=False, indent=2), encoding="utf-8")
    args.summary_json.write_text(json.dumps(summary, ensure_ascii=False, indent=2), encoding="utf-8")
    fields = [
        "model", "group", "operation_index", "type_hash", "target", "source",
        "is_clp_root", "clp_depth", "no_up", "no_down", "metadata", "properties",
    ]
    with args.csv.open("w", encoding="utf-8-sig", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields)
        writer.writeheader()
        for row in overlaps:
            writer.writerow({**row, "properties": json.dumps(row["properties"], ensure_ascii=False)})
    print(json.dumps(summary, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
