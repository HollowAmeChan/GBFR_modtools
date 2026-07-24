#!/usr/bin/env python3
"""Compare two analyze_mmat.py material datasets by decoded semantics."""

from __future__ import annotations

import argparse
import collections
import json
import pathlib


STATE_FIELDS = ("shader", "shadow", "bool9", "bool10", "ignore_alpha", "bool12")


def load_records(path: pathlib.Path) -> dict[tuple[str, str, int], dict]:
    records: dict[tuple[str, str, int], dict] = {}
    occurrences: collections.Counter[tuple[str, str]] = collections.Counter()
    with path.open("r", encoding="utf-8") as stream:
        for line in stream:
            record = json.loads(line)
            base_key = (record["file"], record["material_hash"])
            occurrence = occurrences[base_key]
            occurrences[base_key] += 1
            records[(*base_key, occurrence)] = record
    return records


def normalize_params(record: dict) -> list[dict]:
    result = []
    for parameter in record.get("params", []):
        item = {"hash": parameter["hash"], "type": parameter["type"]}
        if 2 <= parameter["type"] <= 5:
            item["value"] = parameter.get("floats", [])
        else:
            item["value"] = parameter.get("raw")
        result.append(item)
    return result


def referenced_buffers(record: dict) -> list[dict]:
    buffers = record.get("buffers", [])
    result = []
    for index in record.get("buffer_indices", []):
        if 0 <= index < len(buffers):
            result.append(buffers[index])
        else:
            result.append({"invalid_index": index})
    return result


def semantic_parts(record: dict) -> dict:
    return {
        "state": {field: record.get(field) for field in STATE_FIELDS},
        "params": normalize_params(record),
        "maps": record.get("maps", []),
        "buffers": referenced_buffers(record),
        "granite": record.get("granite"),
    }


def key_json(key: tuple[str, str, int]) -> dict:
    return {"file": key[0], "material_hash": key[1], "occurrence": key[2]}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--baseline", type=pathlib.Path, required=True)
    parser.add_argument("--candidate", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    args = parser.parse_args()

    baseline = load_records(args.baseline)
    candidate = load_records(args.candidate)
    common = sorted(baseline.keys() & candidate.keys())
    component_counts: collections.Counter[str] = collections.Counter()
    differences = []
    for key in common:
        left = semantic_parts(baseline[key])
        right = semantic_parts(candidate[key])
        changed = {}
        for component in left:
            if left[component] != right[component]:
                component_counts[component] += 1
                changed[component] = {"baseline": left[component], "candidate": right[component]}
        if changed:
            differences.append({**key_json(key), "changed": changed})

    result = {
        "baseline_materials": len(baseline),
        "candidate_materials": len(candidate),
        "matched_materials": len(common),
        "changed_materials": len(differences),
        "component_counts": dict(sorted(component_counts.items())),
        "missing_from_candidate": [key_json(key) for key in sorted(baseline.keys() - candidate.keys())],
        "extra_in_candidate": [key_json(key) for key in sorted(candidate.keys() - baseline.keys())],
        "differences": differences,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    counts = ", ".join(f"{key}={value}" for key, value in result["component_counts"].items()) or "none"
    print(
        f"matched={result['matched_materials']} changed={result['changed_materials']} "
        f"missing={len(result['missing_from_candidate'])} extra={len(result['extra_in_candidate'])} "
        f"components: {counts}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
