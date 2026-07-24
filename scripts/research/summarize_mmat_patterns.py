#!/usr/bin/env python3
"""Summarize selected MMAT parameter values and pair relationships."""

from __future__ import annotations

import argparse
import collections
import csv
import json
import pathlib


def parse_hash(text: str) -> str:
    return f"0x{int(text, 0):08X}"


def display_value(param: dict) -> str:
    floats = param.get("floats") or []
    if floats:
        return "float:" + ",".join(f"{value:.7g}" for value in floats)
    return f"raw:{param.get('raw', 0)}"


def top(counter: collections.Counter, limit: int = 12) -> list[dict]:
    return [{"value": key, "count": count} for key, count in counter.most_common(limit)]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--materials", type=pathlib.Path, required=True)
    parser.add_argument("--hash", dest="hashes", action="append", required=True,
                        help="Parameter hash; repeat to compare several parameters")
    parser.add_argument("--out-dir", type=pathlib.Path, required=True)
    parser.add_argument("--example-limit", type=int, default=12)
    args = parser.parse_args()

    selected = [parse_hash(value) for value in args.hashes]
    selected_set = set(selected)
    values = {hash_value: collections.Counter() for hash_value in selected}
    categories = {hash_value: collections.Counter() for hash_value in selected}
    shaders = {hash_value: collections.Counter() for hash_value in selected}
    examples: dict[tuple[str, str], list[dict]] = collections.defaultdict(list)
    pair_values: dict[tuple[str, str], collections.Counter] = {}
    pair_presence: dict[tuple[str, str], collections.Counter] = {}
    for left_index, left in enumerate(selected):
        for right in selected[left_index + 1:]:
            pair_values[(left, right)] = collections.Counter()
            pair_presence[(left, right)] = collections.Counter()

    with args.materials.open("r", encoding="utf-8") as stream:
        for line in stream:
            record = json.loads(line)
            found = {
                param["hash"]: display_value(param)
                for param in record.get("params", [])
                if param["hash"] in selected_set
            }
            for hash_value, value in found.items():
                values[hash_value][value] += 1
                categories[hash_value][record["category"]] += 1
                shaders[hash_value][record["shader"]] += 1
                key = (hash_value, value)
                if len(examples[key]) < args.example_limit:
                    examples[key].append({
                        "file": record["file"],
                        "material": record["material"],
                        "material_hash": record["material_hash"],
                        "shader": record["shader"],
                        "shadow": record["shadow"],
                        "ignore_alpha": record["ignore_alpha"],
                        "maps": [item["texture"] for item in record.get("maps", [])],
                    })
            for pair in pair_values:
                left, right = pair
                left_present = left in found
                right_present = right in found
                pair_presence[pair][f"{int(left_present)}/{int(right_present)}"] += 1
                if left_present and right_present:
                    pair_values[pair][f"{found[left]} | {found[right]}"] += 1

    out_dir = args.out_dir.resolve()
    out_dir.mkdir(parents=True, exist_ok=True)
    report = {
        "parameters": {
            hash_value: {
                "occurrences": sum(values[hash_value].values()),
                "values": top(values[hash_value], 64),
                "categories": top(categories[hash_value]),
                "shaders": top(shaders[hash_value]),
                "examples_by_value": {
                    value: examples[(hash_value, value)] for value in values[hash_value]
                },
            }
            for hash_value in selected
        },
        "pairs": {
            f"{left}/{right}": {
                "presence": dict(pair_presence[(left, right)]),
                "values": top(pair_values[(left, right)], 64),
            }
            for left, right in pair_values
        },
    }
    (out_dir / "parameter_patterns.json").write_text(
        json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")

    with (out_dir / "parameter_pair_values.csv").open("w", encoding="utf-8-sig", newline="") as stream:
        writer = csv.writer(stream)
        writer.writerow(["left_hash", "right_hash", "left_value", "right_value", "materials"])
        for (left, right), counter in pair_values.items():
            for combined, count in counter.most_common():
                left_value, right_value = combined.split(" | ", 1)
                writer.writerow([left, right, left_value, right_value, count])

    print(json.dumps({
        "parameters": {hash_value: sum(counter.values()) for hash_value, counter in values.items()},
        "pair_count": len(pair_values),
        "output": str(out_dir),
    }, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
