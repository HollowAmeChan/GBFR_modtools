#!/usr/bin/env python3
"""Compare two character MMAT cohorts produced by analyze_character_mmat.py."""

from __future__ import annotations

import argparse
import collections
import csv
import json
import pathlib


def load_rows(path: pathlib.Path, model: str, variant: str) -> list[dict]:
    with path.open("r", encoding="utf-8-sig", newline="") as stream:
        return [row for row in csv.DictReader(stream)
                if row["model"] == model and row["variant"] == variant]


def counts(values) -> dict[str, int]:
    return dict(sorted(collections.Counter(values).items()))


def material_summary(rows: list[dict]) -> dict[str, dict]:
    by_family: dict[str, list[dict]] = collections.defaultdict(list)
    for row in rows:
        by_family[row["family"]].append(row)
    result = {}
    for family, items in sorted(by_family.items()):
        result[family] = {
            "materials": len(items),
            "shaders": sorted({row["shader"] for row in items}),
            "states": counts(row["state"] for row in items),
            "parameter_hash_signatures": sorted({row["parameter_hashes"] for row in items}),
            "map_hash_signatures": sorted({row["map_hashes"] for row in items}),
            "first_buffer_hashes": sorted({row["first_buffer_hash"] for row in items}),
            "first_buffer_values": sorted({row["first_buffer_sha256_16"] for row in items}),
        }
    return result


def field_summary(rows: list[dict]) -> dict[tuple[str, str], collections.Counter]:
    result: dict[tuple[str, str], collections.Counter] = collections.defaultdict(collections.Counter)
    for row in rows:
        result[(row["family"], row["field"])][row["value"]] += 1
    return result


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--materials", type=pathlib.Path, required=True)
    parser.add_argument("--fields", type=pathlib.Path, required=True)
    parser.add_argument("--left-model", required=True)
    parser.add_argument("--right-model", required=True)
    parser.add_argument("--left-variant", default="0")
    parser.add_argument("--right-variant", default="0")
    parser.add_argument("--output", type=pathlib.Path, required=True)
    args = parser.parse_args()

    left_materials = load_rows(args.materials.resolve(), args.left_model, args.left_variant)
    right_materials = load_rows(args.materials.resolve(), args.right_model, args.right_variant)
    left_fields = load_rows(args.fields.resolve(), args.left_model, args.left_variant)
    right_fields = load_rows(args.fields.resolve(), args.right_model, args.right_variant)
    if not left_materials:
        raise ValueError(f"no material rows for {args.left_model} variant {args.left_variant}")
    if not right_materials:
        raise ValueError(f"no material rows for {args.right_model} variant {args.right_variant}")

    left_families = material_summary(left_materials)
    right_families = material_summary(right_materials)
    family_differences = {}
    for family in sorted(set(left_families) | set(right_families)):
        left = left_families.get(family)
        right = right_families.get(family)
        if left is None or right is None:
            family_differences[family] = {"left": left, "right": right}
            continue
        changed = {
            key: {"left": left[key], "right": right[key]}
            for key in ("shaders", "states", "parameter_hash_signatures", "map_hash_signatures",
                        "first_buffer_hashes", "first_buffer_values")
            if left[key] != right[key]
        }
        if left["materials"] != right["materials"]:
            changed["materials"] = {"left": left["materials"], "right": right["materials"]}
        if changed:
            family_differences[family] = changed

    left_field_values = field_summary(left_fields)
    right_field_values = field_summary(right_fields)
    field_differences = []
    for family, field in sorted(set(left_field_values) | set(right_field_values)):
        left = left_field_values.get((family, field), collections.Counter())
        right = right_field_values.get((family, field), collections.Counter())
        if set(left) != set(right):
            field_differences.append({
                "family": family,
                "field": field,
                "left_values": dict(left),
                "right_values": dict(right),
            })

    report = {
        "left": {"model": args.left_model, "variant": args.left_variant,
                 "materials": len(left_materials), "families": left_families},
        "right": {"model": args.right_model, "variant": args.right_variant,
                  "materials": len(right_materials), "families": right_families},
        "family_differences": family_differences,
        "field_value_set_differences": field_differences,
    }
    args.output.resolve().parent.mkdir(parents=True, exist_ok=True)
    args.output.resolve().write_text(
        json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
    print(json.dumps({
        "left_materials": len(left_materials),
        "right_materials": len(right_materials),
        "family_differences": len(family_differences),
        "field_value_set_differences": len(field_differences),
        "output": str(args.output.resolve()),
    }, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
