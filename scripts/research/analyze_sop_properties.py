#!/usr/bin/env python3
"""Profile SOP property layouts and values across extracted player resources."""

from __future__ import annotations

import argparse
import csv
import json
from collections import Counter, defaultdict
from pathlib import Path
from typing import Any

from analyze_sop_cloth_overlap import load_sop


def analyze(data_root: Path) -> dict[str, Any]:
    operations_by_type: Counter[int] = Counter()
    models_by_type: dict[int, set[str]] = defaultdict(set)
    signatures: dict[int, Counter[tuple[tuple[str, str], ...]]] = defaultdict(Counter)
    properties: dict[int, dict[str, dict[str, Any]]] = defaultdict(dict)

    for sop_path in sorted((data_root / "model" / "pl").glob("pl*/pl*.sop")):
        model = sop_path.stem.casefold()
        for operation in load_sop(sop_path):
            type_hash = int(operation["type_hash"])
            operations_by_type[type_hash] += 1
            models_by_type[type_hash].add(model)
            signature = tuple((prop["hash"], prop["type"]) for prop in operation["properties"])
            signatures[type_hash][signature] += 1
            for position, prop in enumerate(operation["properties"]):
                prop_hash = str(prop["hash"])
                profile = properties[type_hash].setdefault(prop_hash, {
                    "hash": prop_hash,
                    "occurrences": 0,
                    "types": Counter(),
                    "positions": Counter(),
                    "values": Counter(),
                    "models": set(),
                })
                profile["occurrences"] += 1
                profile["types"][str(prop["type"])] += 1
                profile["positions"][position] += 1
                profile["values"][repr(prop["value"])] += 1
                profile["models"].add(model)

    result = {"types": []}
    for type_hash in sorted(operations_by_type):
        operation_count = operations_by_type[type_hash]
        type_profiles = []
        for profile in properties[type_hash].values():
            numeric_values = [
                float(value) for value in profile["values"]
                if value not in {"nan", "inf", "-inf"}
            ]
            type_profiles.append({
                "hash": profile["hash"],
                "occurrences": profile["occurrences"],
                "coverage": profile["occurrences"] / operation_count,
                "models": len(profile["models"]),
                "types": dict(profile["types"].most_common()),
                "positions": {str(key): value for key, value in sorted(profile["positions"].items())},
                "unique_values": len(profile["values"]),
                "minimum": min(numeric_values) if numeric_values else None,
                "maximum": max(numeric_values) if numeric_values else None,
                "common_values": [
                    {"value": value, "count": count}
                    for value, count in profile["values"].most_common(12)
                ],
            })
        result["types"].append({
            "type_hash": f"0x{type_hash:08X}",
            "operations": operation_count,
            "models": len(models_by_type[type_hash]),
            "signatures": [
                {
                    "count": count,
                    "properties": [
                        {"hash": prop_hash, "type": value_type}
                        for prop_hash, value_type in signature
                    ],
                }
                for signature, count in signatures[type_hash].most_common()
            ],
            "properties": sorted(type_profiles, key=lambda item: min(map(int, item["positions"]))),
        })
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("data_root", type=Path, help="Extracted GBFR data directory")
    parser.add_argument("--json", type=Path, required=True)
    parser.add_argument("--csv", type=Path, required=True)
    args = parser.parse_args()

    result = analyze(args.data_root)
    args.json.parent.mkdir(parents=True, exist_ok=True)
    args.csv.parent.mkdir(parents=True, exist_ok=True)
    args.json.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
    fields = [
        "type_hash", "operations", "models", "property_hash", "occurrences", "coverage",
        "property_models", "types", "positions", "unique_values", "minimum", "maximum",
        "common_values",
    ]
    with args.csv.open("w", encoding="utf-8-sig", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields)
        writer.writeheader()
        for type_profile in result["types"]:
            for prop in type_profile["properties"]:
                writer.writerow({
                    "type_hash": type_profile["type_hash"],
                    "operations": type_profile["operations"],
                    "models": type_profile["models"],
                    "property_hash": prop["hash"],
                    "occurrences": prop["occurrences"],
                    "coverage": prop["coverage"],
                    "property_models": prop["models"],
                    "types": json.dumps(prop["types"], ensure_ascii=False),
                    "positions": json.dumps(prop["positions"], ensure_ascii=False),
                    "unique_values": prop["unique_values"],
                    "minimum": prop["minimum"],
                    "maximum": prop["maximum"],
                    "common_values": json.dumps(prop["common_values"], ensure_ascii=False),
                })
    print(json.dumps({
        "types": len(result["types"]),
        "operations": sum(item["operations"] for item in result["types"]),
        "json": str(args.json),
        "csv": str(args.csv),
    }, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
