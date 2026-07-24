#!/usr/bin/env python3
"""Build a focused, reproducible dataset for character MMAT materials."""

from __future__ import annotations

import argparse
import collections
import csv
import hashlib
import json
import math
import pathlib
import struct

from generate_mmat_cbuffer_catalog import SOURCES


CHARACTER_CATEGORIES = {"pl", "fp", "wp"}
CHARACTER_SHADER_TYPES = {2, 3, 4, 5, 6}
SHADER_FAMILIES = {
    2: "Eye",
    3: "Face",
    4: "Hair",
    5: "Metal",
    6: "Skin",
}


def shader_type_of(record: dict) -> int:
    return int(record["shader"].split("/", 1)[0])


def model_and_variant(file_name: str) -> tuple[str, str]:
    parts = pathlib.PurePosixPath(file_name).parts
    model = parts[2] if len(parts) > 2 and parts[0] == "model" else ""
    try:
        vars_index = parts.index("vars")
        variant = pathlib.PurePosixPath(parts[vars_index + 1]).stem
    except (ValueError, IndexError):
        variant = ""
    return model, variant


def load_layouts(reflection_path: pathlib.Path) -> dict[int, dict]:
    source_by_path = {
        path: (shader_type, expected_size)
        for shader_type, expected_size, path in SOURCES
        if shader_type in CHARACTER_SHADER_TYPES
    }
    found: dict[str, dict] = {}
    with reflection_path.open("r", encoding="utf-8") as stream:
        for line in stream:
            shader = json.loads(line)
            if shader.get("file") in source_by_path:
                found[shader["file"]] = shader

    layouts: dict[int, dict] = {}
    for path, (shader_type, expected_size) in source_by_path.items():
        shader = found.get(path)
        if shader is None:
            raise ValueError(f"reflection is missing {path}")
        matches = [
            item for item in shader.get("constant_buffers", [])
            if item.get("name") == "ParamBuffer" and item.get("size") == expected_size
        ]
        if len(matches) != 1:
            raise ValueError(
                f"{path} has {len(matches)} matching {expected_size}-byte ParamBuffer entries"
            )
        layouts[shader_type] = {
            "shader": path,
            "size": expected_size,
            "fields": matches[0].get("variables", []),
        }
    return layouts


def words_to_bytes(words: list[int]) -> bytes:
    return b"".join(struct.pack("<I", int(word) & 0xFFFFFFFF) for word in words)


def format_float(value: float) -> str:
    if math.isnan(value):
        return "nan"
    if math.isinf(value):
        return "inf" if value > 0 else "-inf"
    if value == 0.0:
        return "0"
    return format(value, ".9g")


def decode_field(payload: bytes, field: dict) -> tuple[str, str]:
    offset = int(field["offset"])
    size = int(field["size"])
    type_name = field["type"]["name"]
    raw = payload[offset:offset + size]
    if len(raw) != size or size % 4:
        raise ValueError(f"invalid field range {field['name']} offset={offset} size={size}")
    raw_words = ";".join(f"0x{word:08X}" for word in struct.unpack(f"<{size // 4}I", raw))
    if type_name.startswith("float"):
        values = [format_float(value) for value in struct.unpack(f"<{size // 4}f", raw)]
    elif type_name == "bool":
        values = ["false" if value == 0 else "true" if value == 1 else f"raw({value})"
                  for value in struct.unpack(f"<{size // 4}I", raw)]
    elif type_name == "int":
        values = [str(value) for value in struct.unpack(f"<{size // 4}i", raw)]
    elif type_name in {"uint", "dword"}:
        values = [str(value) for value in struct.unpack(f"<{size // 4}I", raw)]
    else:
        raise ValueError(f"unsupported RDEF field type {type_name!r}")
    decoded = values[0] if len(values) == 1 else "[" + ",".join(values) + "]"
    return decoded, raw_words


def signature(items: list[dict], include_values: bool) -> str:
    values = []
    for item in items:
        if include_values:
            values.append(f"{item['hash']}={item.get('texture', '')}")
        else:
            values.append(item["hash"])
    return ";".join(values)


def counter_text(counter: collections.Counter) -> str:
    return "; ".join(f"{key}:{count}" for key, count in counter.most_common())


def load_parameter_catalog(path: pathlib.Path | None) -> dict[str, tuple[str, str]]:
    if path is None:
        return {}
    result = {}
    with path.resolve().open("r", encoding="utf-8-sig", newline="") as stream:
        for row in csv.DictReader(stream):
            result[row["hash"]] = (row.get("resolved_name", ""), row.get("evidence", ""))
    return result


def parameter_value(parameter: dict) -> str:
    floats = parameter.get("floats") or []
    if floats:
        values = [format_float(float(value)) for value in floats]
        return values[0] if len(values) == 1 else "[" + ",".join(values) + "]"
    return str(parameter.get("raw", 0))


def raw_parameter(record: dict, hash_value: str) -> int | None:
    for parameter in record.get("params", []):
        if parameter.get("hash") == hash_value:
            return int(parameter.get("raw", 0))
    return None


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--materials", type=pathlib.Path, required=True)
    parser.add_argument("--reflection", type=pathlib.Path, required=True)
    parser.add_argument("--parameter-catalog", type=pathlib.Path,
                        help="Optional analyze_mmat.py shader_parameters.csv for names and evidence")
    parser.add_argument("--out-dir", type=pathlib.Path, required=True)
    args = parser.parse_args()

    layouts = load_layouts(args.reflection.resolve())
    parameter_catalog = load_parameter_catalog(args.parameter_catalog)
    records: list[dict] = []
    failures: list[str] = []
    scanned = 0
    with args.materials.resolve().open("r", encoding="utf-8") as stream:
        for line in stream:
            scanned += 1
            record = json.loads(line)
            shader_type = shader_type_of(record)
            if record.get("category") not in CHARACTER_CATEGORIES or shader_type not in layouts:
                continue
            layout = layouts[shader_type]
            indices = record.get("buffer_indices", [])
            buffers = record.get("buffers", [])
            index = int(indices[0]) if indices else -1
            actual_size = len(buffers[index]["words"]) * 4 if 0 <= index < len(buffers) else -1
            if actual_size != layout["size"]:
                failures.append(
                    f"{record['file']}#{record['material']} type={shader_type} "
                    f"expected={layout['size']} actual={actual_size} index={index}"
                )
                continue
            record["_layout"] = layout
            record["_first_buffer_index"] = index
            records.append(record)

    if failures:
        raise ValueError("character ParamBuffer mismatches:\n" + "\n".join(failures[:40]))

    out_dir = args.out_dir.resolve()
    out_dir.mkdir(parents=True, exist_ok=True)
    material_rows: list[dict] = []
    field_rows: list[dict] = []
    parameter_rows: list[dict] = []
    family_stats: dict[tuple[str, str], dict] = {}
    field_stats: collections.Counter = collections.Counter()
    parameter_stats: collections.Counter = collections.Counter()

    for record in records:
        shader_type = shader_type_of(record)
        family = SHADER_FAMILIES[shader_type]
        model, variant = model_and_variant(record["file"])
        buffer_index = record["_first_buffer_index"]
        buffer = record["buffers"][buffer_index]
        payload = words_to_bytes(buffer["words"])
        params = record.get("params", [])
        maps = record.get("maps", [])
        granite = record.get("granite")
        state = (
            f"shadow={record['shadow']},bool9={int(record['bool9'])},"
            f"bool10={int(record['bool10'])},ignore_alpha={int(record['ignore_alpha'])},"
            f"bool12={int(record['bool12'])}"
        )
        param_hash_signature = signature(params, False)
        map_hash_signature = signature(maps, False)
        map_value_signature = signature(maps, True)
        fingerprint = hashlib.sha256(payload).hexdigest()[:16]
        row = {
            "file": record["file"],
            "category": record["category"],
            "model": model,
            "variant": variant,
            "material": record["material"],
            "material_hash": record["material_hash"],
            "shader": record["shader"],
            "family": family,
            "state": state,
            "parameter_hashes": param_hash_signature,
            "map_hashes": map_hash_signature,
            "maps": map_value_signature,
            "constant_buffer_indices": ";".join(str(value) for value in record.get("buffer_indices", [])),
            "first_buffer_index": buffer_index,
            "first_buffer_hash": buffer["hash"],
            "first_buffer_size": len(payload),
            "first_buffer_sha256_16": fingerprint,
            "granite_tile": granite.get("tile", "") if granite else "",
            "granite_pages": len(granite.get("pages", [])) if granite else 0,
            "granite_layers": ";".join(granite.get("layers", [])) if granite else "",
        }
        material_rows.append(row)

        for parameter in params:
            name, evidence = parameter_catalog.get(parameter["hash"], ("", ""))
            value = parameter_value(parameter)
            parameter_row = {
                "file": record["file"],
                "category": record["category"],
                "model": model,
                "variant": variant,
                "material": record["material"],
                "material_hash": record["material_hash"],
                "shader": record["shader"],
                "family": family,
                "hash": parameter["hash"],
                "name": name,
                "evidence": evidence,
                "type": parameter.get("type", ""),
                "raw": parameter.get("raw", ""),
                "value": value,
            }
            parameter_rows.append(parameter_row)
            parameter_stats[(record["category"], record["shader"], family,
                             parameter["hash"], name, evidence,
                             parameter.get("type", ""), value)] += 1

        key = (record["category"], record["shader"])
        stat = family_stats.setdefault(key, {
            "materials": 0,
            "files": set(),
            "models": set(),
            "variants": set(),
            "states": collections.Counter(),
            "parameter_signatures": collections.Counter(),
            "map_signatures": collections.Counter(),
            "buffer_hashes": collections.Counter(),
            "buffer_values": collections.Counter(),
            "granite": 0,
        })
        stat["materials"] += 1
        stat["files"].add(record["file"])
        stat["models"].add(model)
        stat["variants"].add((model, variant))
        stat["states"][state] += 1
        stat["parameter_signatures"][param_hash_signature] += 1
        stat["map_signatures"][map_hash_signature] += 1
        stat["buffer_hashes"][buffer["hash"]] += 1
        stat["buffer_values"][fingerprint] += 1
        stat["granite"] += int(granite is not None)

        for field in record["_layout"]["fields"]:
            value, raw_words = decode_field(payload, field)
            field_row = {
                "file": record["file"],
                "category": record["category"],
                "model": model,
                "variant": variant,
                "material": record["material"],
                "material_hash": record["material_hash"],
                "shader": record["shader"],
                "family": family,
                "field": field["name"],
                "offset": field["offset"],
                "size": field["size"],
                "type": field["type"]["name"],
                "value": value,
                "raw_words": raw_words,
            }
            field_rows.append(field_row)
            field_stats[(record["category"], record["shader"], family,
                         field["name"], field["type"]["name"], value)] += 1

    material_fields = list(material_rows[0]) if material_rows else []
    with (out_dir / "character_materials.csv").open("w", encoding="utf-8-sig", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=material_fields)
        writer.writeheader()
        writer.writerows(material_rows)

    field_names = list(field_rows[0]) if field_rows else []
    with (out_dir / "character_parambuffer_fields.csv").open("w", encoding="utf-8-sig", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=field_names)
        writer.writeheader()
        writer.writerows(field_rows)

    parameter_fields = list(parameter_rows[0]) if parameter_rows else []
    with (out_dir / "character_shader_parameters.csv").open("w", encoding="utf-8-sig", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=parameter_fields)
        writer.writeheader()
        writer.writerows(parameter_rows)

    family_rows = []
    for (category, shader), stat in sorted(family_stats.items()):
        shader_type = int(shader.split("/", 1)[0])
        family_rows.append({
            "category": category,
            "shader": shader,
            "family": SHADER_FAMILIES[shader_type],
            "materials": stat["materials"],
            "files": len(stat["files"]),
            "models": len(stat["models"]),
            "model_variants": len(stat["variants"]),
            "granite_materials": stat["granite"],
            "state_combinations": len(stat["states"]),
            "states": counter_text(stat["states"]),
            "parameter_signatures": len(stat["parameter_signatures"]),
            "map_signatures": len(stat["map_signatures"]),
            "first_buffer_hashes": len(stat["buffer_hashes"]),
            "first_buffer_values": len(stat["buffer_values"]),
        })
    family_fields = list(family_rows[0]) if family_rows else []
    with (out_dir / "character_families.csv").open("w", encoding="utf-8-sig", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=family_fields)
        writer.writeheader()
        writer.writerows(family_rows)

    with (out_dir / "character_field_values.csv").open("w", encoding="utf-8-sig", newline="") as stream:
        writer = csv.writer(stream)
        writer.writerow(["category", "shader", "family", "field", "type", "value", "materials"])
        for key, count in sorted(field_stats.items()):
            writer.writerow([*key, count])

    with (out_dir / "character_shader_parameter_values.csv").open("w", encoding="utf-8-sig", newline="") as stream:
        writer = csv.writer(stream)
        writer.writerow(["category", "shader", "family", "hash", "name", "evidence", "type", "value", "materials"])
        for key, count in sorted(parameter_stats.items()):
            writer.writerow([*key, count])

    alpha_key_missing = alpha_key_mismatches = 0
    shadow_state_mismatches = 0
    bool9_true = bool10_false = 0
    two_sided_present = two_sided_enabled = 0
    discard_present = discard_enabled = 0
    subtype13_present = subtype13_enabled = 0
    directional_alpha_present = directional_alpha_enabled = directional_alpha_mismatches = 0
    alternate_emissive_present = alternate_emissive_enabled = 0
    alternate_emissive_models: set[str] = set()
    alternate_emissive_variants: set[str] = set()
    for record in records:
        alpha_key = raw_parameter(record, "0x53F49792")
        if alpha_key is None:
            alpha_key_missing += 1
        elif bool(alpha_key) != (not bool(record["ignore_alpha"])):
            alpha_key_mismatches += 1
        shadow_state_mismatches += int(bool(record["bool12"]) != (int(record["shadow"]) == 3))
        bool9_true += int(bool(record["bool9"]))
        bool10_false += int(not bool(record["bool10"]))
        two_sided = raw_parameter(record, "0xD94F2821")
        discard = raw_parameter(record, "0x24C1ABA9")
        subtype13 = raw_parameter(record, "0x8B8038FC")
        directional_alpha = raw_parameter(record, "0xA6EB1B34")
        alternate_emissive = raw_parameter(record, "0x9C83F56F")
        if two_sided is not None:
            two_sided_present += 1
            two_sided_enabled += int(bool(two_sided))
        if discard is not None:
            discard_present += 1
            discard_enabled += int(bool(discard))
        if subtype13 is not None:
            subtype13_present += 1
            subtype13_enabled += int(bool(subtype13))
        if directional_alpha is not None:
            directional_alpha_present += 1
            directional_alpha_enabled += int(bool(directional_alpha))
            directional_alpha_mismatches += int(
                bool(directional_alpha) != (int(record["shadow"]) == 3 and bool(record["bool12"]))
            )
        if alternate_emissive is not None:
            alternate_emissive_present += 1
            alternate_emissive_enabled += int(bool(alternate_emissive))
            if alternate_emissive:
                model, variant = model_and_variant(record["file"])
                alternate_emissive_models.add(model)
                alternate_emissive_variants.add(variant)

    invariants = {
        "scope": "model/{pl,fp,wp} with character shader types 2..6",
        "materials": len(records),
        "alpha_pass_key_0x53F49792": {
            "present": len(records) - alpha_key_missing,
            "missing": alpha_key_missing,
            "mismatches_against_not_ignore_alpha": alpha_key_mismatches,
        },
        "bool12_matches_shadow_type_3": {
            "matches": len(records) - shadow_state_mismatches,
            "mismatches": shadow_state_mismatches,
        },
        "fixed_root_states": {"bool9_true": bool9_true, "bool10_false": bool10_false},
        "g_TwoSided_0xD94F2821": {"present": two_sided_present, "enabled": two_sided_enabled},
        "g_EnableDiscardMask_0x24C1ABA9": {"present": discard_present, "enabled": discard_enabled},
        "subtype13_selector_0x8B8038FC": {"present": subtype13_present, "enabled": subtype13_enabled},
        "metal_directional_alpha_cutoff_0xA6EB1B34": {
            "present": directional_alpha_present,
            "enabled": directional_alpha_enabled,
            "mismatches_against_shadow_type_3_and_bool12": directional_alpha_mismatches,
        },
        "metal_alternate_runtime_emissive_factor_0x9C83F56F": {
            "present": alternate_emissive_present,
            "enabled": alternate_emissive_enabled,
            "enabled_models": sorted(alternate_emissive_models),
            "enabled_variants": sorted(alternate_emissive_variants),
            "runtime_behavior": "shading sub-record [2] selects runtime +0x0C instead of +0x08; shader multiplies sub-record [2] * [3] in g_EnableEmissive branch",
        },
    }
    (out_dir / "character_invariants.json").write_text(
        json.dumps(invariants, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )

    summary = {
        "input_material_records": scanned,
        "character_materials": len(material_rows),
        "character_files": len({row["file"] for row in material_rows}),
        "character_models": len({(row["category"], row["model"]) for row in material_rows}),
        "parambuffer_fields": len(field_rows),
        "shader_parameters": len(parameter_rows),
        "layout_mismatches": len(failures),
        "categories": dict(collections.Counter(row["category"] for row in material_rows).most_common()),
        "families": dict(collections.Counter(row["family"] for row in material_rows).most_common()),
        "outputs": [
            "character_materials.csv",
            "character_parambuffer_fields.csv",
            "character_shader_parameters.csv",
            "character_families.csv",
            "character_field_values.csv",
            "character_shader_parameter_values.csv",
            "character_invariants.json",
        ],
    }
    (out_dir / "summary.json").write_text(
        json.dumps(summary, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
    print(json.dumps(summary, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
