#!/usr/bin/env python3
"""Build a reproducible MMAT/Direct3D shader correlation dataset."""

from __future__ import annotations

import argparse
import collections
import csv
import importlib
import json
import pathlib
import re
import struct
import subprocess
import sys
import tempfile
from dataclasses import dataclass, field


MASK32 = 0xFFFFFFFF
PRIME1 = 0x9E3779B1
PRIME2 = 0x85EBCA77
PRIME3 = 0xC2B2AE3D
PRIME4 = 0x27D4EB2F
PRIME5 = 0x165667B1

# These names have no RDEF or executable-string evidence. Each is an exact
# custom-XXHash32 preimage corroborated by every occurrence in the full MMAT set.
INFERRED_TEXTURE_NAMES = {
    0x5A2C820C: "g_OutlineTexture",
    0x8A0507FB: "g_Mask5",
}


def rotl(value: int, bits: int) -> int:
    return ((value << bits) | (value >> (32 - bits))) & MASK32


def xxhash32_custom(text: str) -> int:
    data = text.encode("ascii", errors="replace")
    remaining = memoryview(data)
    hash_value = 0x178A54A4
    if len(remaining) >= 16:
        values = [0x2557311B, 0x871FB76A, 0x0133ECF3, 0x62FC7342]
        while True:
            for index in range(4):
                word = struct.unpack_from("<I", remaining, index * 4)[0]
                values[index] = (rotl((values[index] + word * PRIME2) & MASK32, 13) * PRIME1) & MASK32
            remaining = remaining[16:]
            if len(remaining) <= 16:
                break
        hash_value = sum(rotl(values[index], (1, 7, 12, 18)[index]) for index in range(4)) & MASK32
    hash_value = (hash_value + len(data)) & MASK32
    while len(remaining) >= 4:
        word = struct.unpack_from("<I", remaining, 0)[0]
        hash_value = (rotl((hash_value + word * PRIME3) & MASK32, 17) * PRIME4) & MASK32
        remaining = remaining[4:]
    for byte in remaining:
        hash_value = (rotl((hash_value + byte * PRIME5) & MASK32, 11) * PRIME1) & MASK32
    hash_value ^= hash_value >> 15
    hash_value = (hash_value * PRIME2) & MASK32
    hash_value ^= hash_value >> 13
    hash_value = (hash_value * PRIME3) & MASK32
    hash_value ^= hash_value >> 16
    return hash_value & MASK32


def enum_names(module_name: str, class_name: str) -> dict[int, str]:
    module = importlib.import_module(module_name)
    enum_class = getattr(module, class_name)
    return {value: name for name, value in vars(enum_class).items() if not name.startswith("_") and isinstance(value, int)}


def decode_text(value: bytes | None) -> str:
    return value.decode("utf-8", errors="replace") if value else ""


def category_for(path: pathlib.Path, data_root: pathlib.Path) -> str:
    parts = path.relative_to(data_root).parts
    if len(parts) >= 2 and parts[0].lower() == "model":
        return parts[1].lower()
    return parts[0].lower() if parts else "unknown"


def top(counter: collections.Counter, count: int = 8) -> str:
    return "; ".join(f"{key}:{value}" for key, value in counter.most_common(count))


@dataclass
class NameEvidence:
    names: collections.Counter = field(default_factory=collections.Counter)
    kinds: collections.Counter = field(default_factory=collections.Counter)
    files: set[str] = field(default_factory=set)
    examples: list[str] = field(default_factory=list)


@dataclass
class ParamStat:
    materials: int = 0
    files: set[str] = field(default_factory=set)
    categories: collections.Counter = field(default_factory=collections.Counter)
    shaders: collections.Counter = field(default_factory=collections.Counter)
    value_types: collections.Counter = field(default_factory=collections.Counter)
    raw_values: collections.Counter = field(default_factory=collections.Counter)
    float_values: collections.Counter = field(default_factory=collections.Counter)
    shadows: collections.Counter = field(default_factory=collections.Counter)
    maps: collections.Counter = field(default_factory=collections.Counter)
    granite: int = 0
    ignore_alpha: int = 0
    bool9: int = 0
    bool10: int = 0
    bool12: int = 0
    examples: list[str] = field(default_factory=list)


def load_shader_names(shader_jsonl: pathlib.Path) -> dict[int, NameEvidence]:
    evidence: dict[int, NameEvidence] = {}

    def register(name: str, kind: str, file_name: str) -> None:
        if not name or not name.isascii():
            return
        hash_value = xxhash32_custom(name)
        item = evidence.setdefault(hash_value, NameEvidence())
        item.names[name] += 1
        item.kinds[kind] += 1
        item.files.add(file_name)
        if len(item.examples) < 6 and file_name not in item.examples:
            item.examples.append(file_name)

    with shader_jsonl.open("r", encoding="utf-8") as stream:
        for line in stream:
            item = json.loads(line)
            file_name = item.get("file", "")
            for buffer in item.get("constant_buffers", []):
                register(buffer.get("name", ""), "constant_buffer", file_name)
                for variable in buffer.get("variables", []):
                    register(variable.get("name", ""), "variable", file_name)
                    for member in (variable.get("type") or {}).get("member_list", []):
                        register(member.get("member_name", ""), "struct_member", file_name)
            for resource in item.get("resources", []):
                register(resource.get("name", ""), "resource", file_name)
    return evidence


def scan_binary_identifiers(path: pathlib.Path, target_hashes: set[int], evidence: dict[int, NameEvidence]) -> int:
    matches = 0
    data = path.read_bytes()
    for found in re.finditer(rb"[A-Za-z_][A-Za-z0-9_]{2,127}\x00", data):
        name = found.group()[:-1].decode("ascii")
        hash_value = xxhash32_custom(name)
        if hash_value not in target_hashes:
            continue
        item = evidence.setdefault(hash_value, NameEvidence())
        item.names[name] += 1
        item.kinds["executable_string"] += 1
        item.files.add(path.name)
        marker = f"{path.name}@0x{found.start():X}"
        if len(item.examples) < 8 and marker not in item.examples:
            item.examples.append(marker)
        matches += 1
    return matches


def best_name(hash_value: int, schema_names: dict[int, str], shader_names: dict[int, NameEvidence],
              inferred_names: dict[int, str] | None = None) -> tuple[str, str]:
    evidence = shader_names.get(hash_value)
    if evidence:
        names = [name for name, _ in evidence.names.most_common()]
        preferred = next((name for name in names if name.startswith("g_")), names[0])
        return preferred, "shader_reflection"
    schema = schema_names.get(hash_value, "")
    if schema and not (len(schema) >= 10 and schema[2:10].upper() == f"{hash_value:08X}"):
        return schema, "schema_named"
    if inferred_names and hash_value in inferred_names:
        return inferred_names[hash_value], "hash_preimage_full_samples"
    return schema or f"g_{hash_value:08X}", "hash_only"


def prepare_generated(flatc: pathlib.Path, schema: pathlib.Path, output: pathlib.Path) -> None:
    subprocess.run([str(flatc), "--python", "-o", str(output), str(schema)], check=True)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--data-root", type=pathlib.Path, required=True)
    parser.add_argument("--flatc", type=pathlib.Path, required=True)
    parser.add_argument("--schema", type=pathlib.Path, required=True)
    parser.add_argument("--flatbuffers-runtime", type=pathlib.Path, required=True,
                        help="Directory containing the bundled flatbuffers Python package")
    parser.add_argument("--shader-jsonl", type=pathlib.Path, required=True)
    parser.add_argument("--string-binary", type=pathlib.Path, action="append", default=[])
    parser.add_argument("--out-dir", type=pathlib.Path, required=True)
    args = parser.parse_args()
    data_root = args.data_root.resolve()
    out_dir = args.out_dir.resolve()
    out_dir.mkdir(parents=True, exist_ok=True)

    with tempfile.TemporaryDirectory(prefix="gbfr_mmat_python_") as generated_text:
        generated = pathlib.Path(generated_text)
        prepare_generated(args.flatc.resolve(), args.schema.resolve(), generated)
        sys.path.insert(0, str(args.flatbuffers_runtime.resolve()))
        sys.path.insert(0, str(generated))
        from GBFRDataTools.FlatBuffers.ModelMaterialSet import ModelMaterialSet

        parameter_schema = enum_names("GBFRDataTools.FlatBuffers.ShaderParameterTypeNameHash", "ShaderParameterTypeNameHash")
        map_schema = enum_names("GBFRDataTools.FlatBuffers.ShaderMapTypeHash", "ShaderMapTypeHash")
        shader_names = load_shader_names(args.shader_jsonl.resolve())
        if xxhash32_custom("g_AlbedoMap") != 0x3F2B4D59:
            raise RuntimeError("XXHash32Custom implementation does not match GBFRDataTools")
        for expected_hash, inferred_name in INFERRED_TEXTURE_NAMES.items():
            if xxhash32_custom(inferred_name) != expected_hash:
                raise RuntimeError(f"inferred texture name does not hash to 0x{expected_hash:08X}: {inferred_name}")

        parameter_stats: dict[int, ParamStat] = {}
        map_stats: dict[int, ParamStat] = {}
        buffer_stats: dict[int, ParamStat] = {}
        states: collections.Counter = collections.Counter()
        granite_states: collections.Counter = collections.Counter()
        file_categories: collections.Counter = collections.Counter()
        failures: list[dict] = []
        file_count = material_count = 0
        records_path = out_dir / "materials.jsonl"
        with records_path.open("w", encoding="utf-8", newline="\n") as records:
            for path in sorted(data_root.rglob("*.mmat")):
                relative = path.relative_to(data_root).as_posix()
                category = category_for(path, data_root)
                try:
                    payload = path.read_bytes()
                    root = ModelMaterialSet.GetRootAs(payload, 0)
                    if root.Magic() != 20230727:
                        raise ValueError(f"unexpected magic {root.Magic()}")
                    file_count += 1
                    file_categories[category] += 1
                    float_pool = [root.ShaderParamFloatDataPool(i) for i in range(root.ShaderParamFloatDataPoolLength())]
                    buffers = []
                    for buffer_index in range(root.ConstantBuffersLength()):
                        buffer = root.ConstantBuffers(buffer_index)
                        words = [buffer.Buffer(i) for i in range(buffer.BufferLength())]
                        buffer_hash = buffer.UnkUniqueParamNameHash()
                        buffers.append({"hash": f"0x{buffer_hash:08X}", "words": words})
                        stat = buffer_stats.setdefault(buffer_hash, ParamStat())
                        stat.materials += 1; stat.files.add(relative); stat.categories[category] += 1
                        stat.raw_values[str(len(words))] += 1
                        if len(stat.examples) < 8: stat.examples.append(relative)
                    for material_index in range(root.MaterialsLength()):
                        material = root.Materials(material_index)
                        material_count += 1
                        shader_key = f"{material.ShaderType()}/{material.ShaderSubType()}"
                        granite = material.GraniteParams()
                        map_items = []
                        map_hashes = []
                        for map_index in range(material.TextureMapsLength()):
                            texture_map = material.TextureMaps(map_index)
                            map_hash = texture_map.ShaderMapNameHash()
                            texture_name = decode_text(texture_map.TextureName())
                            map_hashes.append(map_hash)
                            map_items.append({"hash": f"0x{map_hash:08X}", "texture": texture_name})
                            stat = map_stats.setdefault(map_hash, ParamStat())
                            stat.materials += 1; stat.files.add(relative); stat.categories[category] += 1
                            stat.shaders[shader_key] += 1; stat.raw_values[texture_name] += 1
                            stat.granite += int(granite is not None); stat.ignore_alpha += int(material.IgnoreAlpha())
                            if len(stat.examples) < 8: stat.examples.append(f"{relative}#{material_index}")
                        params = []
                        for parameter_index in range(material.ShaderParamsLength()):
                            parameter = material.ShaderParams(parameter_index)
                            hash_value = parameter.ParamHash()
                            value_type = parameter.ValueType()
                            raw_value = parameter.ValueOrOffset()
                            component_count = (0, 0, 1, 2, 3, 4)[value_type] if 0 <= value_type <= 5 else 0
                            floats = float_pool[raw_value:raw_value + component_count] if component_count else []
                            params.append({"hash": f"0x{hash_value:08X}", "type": value_type, "raw": raw_value, "floats": floats})
                            stat = parameter_stats.setdefault(hash_value, ParamStat())
                            stat.materials += 1; stat.files.add(relative); stat.categories[category] += 1
                            stat.shaders[shader_key] += 1; stat.value_types[str(value_type)] += 1
                            stat.raw_values[str(raw_value)] += 1
                            if floats: stat.float_values[",".join(f"{value:.7g}" for value in floats)] += 1
                            stat.shadows[str(material.ShadowType())] += 1
                            stat.granite += int(granite is not None); stat.ignore_alpha += int(material.IgnoreAlpha())
                            stat.bool9 += int(material.Bool9()); stat.bool10 += int(material.Bool10()); stat.bool12 += int(material.Bool12())
                            stat.maps.update(f"0x{item:08X}" for item in map_hashes)
                            if len(stat.examples) < 8: stat.examples.append(f"{relative}#{material_index}")
                        state_key = (category, shader_key, material.ShadowType(), material.Bool9(), material.Bool10(),
                                     material.IgnoreAlpha(), material.Bool12(), granite is not None)
                        states[state_key] += 1
                        granite_json = None
                        if granite is not None:
                            granite_key = (category, shader_key, granite.Unk4(), granite.Unk5(), granite.TileSetNumber(),
                                           granite.PageFileLength(), granite.LayerToShaderMapNameHashLength())
                            granite_states[granite_key] += 1
                            granite_json = {"unk4": granite.Unk4(), "unk5": granite.Unk5(), "tile": granite.TileSetNumber(),
                                            "pages": [decode_text(granite.PageFile(i)) for i in range(granite.PageFileLength())],
                                            "layers": [f"0x{granite.LayerToShaderMapNameHash(i):08X}" for i in range(granite.LayerToShaderMapNameHashLength())]}
                        records.write(json.dumps({"file": relative, "category": category, "material": material_index,
                            "material_hash": f"0x{material.UniqueMaterialNameHashMaybe():08X}", "shader": shader_key,
                            "shadow": material.ShadowType(), "bool9": material.Bool9(), "bool10": material.Bool10(),
                            "ignore_alpha": material.IgnoreAlpha(), "bool12": material.Bool12(), "params": params,
                            "maps": map_items, "buffer_indices": [material.ConstantBufferIndices(i) for i in range(material.ConstantBufferIndicesLength())],
                            "buffers": buffers, "granite": granite_json}, ensure_ascii=False, separators=(",", ":")) + "\n")
                except Exception as error:
                    failures.append({"file": relative, "error": str(error)})

        target_hashes=set(parameter_stats)|set(map_stats)|set(buffer_stats)
        binary_matches=0
        for binary in args.string_binary:
            binary_matches+=scan_binary_identifiers(binary.resolve(),target_hashes,shader_names)

        def write_stats(path: pathlib.Path, stats: dict[int, ParamStat], schema_names: dict[int, str],
                        inferred_names: dict[int, str] | None = None) -> None:
            with path.open("w", encoding="utf-8-sig", newline="") as stream:
                writer = csv.writer(stream)
                writer.writerow(["hash","resolved_name","evidence","schema_name","shader_aliases","materials","files",
                                 "value_types","top_values","top_float_values","top_shader_type_subtype","top_categories",
                                 "granite_pct","ignore_alpha_pct","bool9_pct","bool10_pct","bool12_pct","shadow_types",
                                 "cooccurring_maps","shader_files","examples"])
                for hash_value, stat in sorted(stats.items()):
                    resolved, source = best_name(hash_value, schema_names, shader_names, inferred_names)
                    reflected = shader_names.get(hash_value)
                    writer.writerow([f"0x{hash_value:08X}",resolved,source,schema_names.get(hash_value,""),
                        "; ".join(reflected.names if reflected else []),stat.materials,len(stat.files),top(stat.value_types),
                        top(stat.raw_values),top(stat.float_values),top(stat.shaders),top(stat.categories),
                        f"{100*stat.granite/max(1,stat.materials):.2f}",f"{100*stat.ignore_alpha/max(1,stat.materials):.2f}",
                        f"{100*stat.bool9/max(1,stat.materials):.2f}",f"{100*stat.bool10/max(1,stat.materials):.2f}",
                        f"{100*stat.bool12/max(1,stat.materials):.2f}",top(stat.shadows),top(stat.maps),
                        len(reflected.files) if reflected else 0,"; ".join(stat.examples)])

        write_stats(out_dir / "shader_parameters.csv", parameter_stats, parameter_schema)
        write_stats(out_dir / "texture_maps.csv", map_stats, map_schema, INFERRED_TEXTURE_NAMES)
        write_stats(out_dir / "constant_buffers.csv", buffer_stats, {})
        with (out_dir / "shader_hash_catalog.csv").open("w", encoding="utf-8-sig", newline="") as stream:
            writer=csv.writer(stream);writer.writerow(["hash","names","kinds","shader_files","examples"])
            for hash_value,item in sorted(shader_names.items()):
                writer.writerow([f"0x{hash_value:08X}","; ".join(item.names),top(item.kinds),len(item.files),"; ".join(item.examples)])
        with (out_dir / "material_states.csv").open("w",encoding="utf-8-sig",newline="") as stream:
            writer=csv.writer(stream);writer.writerow(["category","shader","shadow","bool9","bool10","ignore_alpha","bool12","granite","materials"])
            for key,count in states.most_common():writer.writerow([*key,count])
        with (out_dir / "granite_states.csv").open("w",encoding="utf-8-sig",newline="") as stream:
            writer=csv.writer(stream);writer.writerow(["category","shader","unk4","unk5","tile","page_count","layer_count","materials"])
            for key,count in granite_states.most_common():writer.writerow([*key,count])
        (out_dir / "failures.json").write_text(json.dumps(failures,ensure_ascii=False,indent=2),encoding="utf-8")
        summary={"files":file_count,"materials":material_count,"failures":len(failures),"parameter_hashes":len(parameter_stats),
                 "texture_map_hashes":len(map_stats),"constant_buffer_hashes":len(buffer_stats),"shader_name_hashes":len(shader_names),
                 "binary_string_matches":binary_matches,"categories":dict(file_categories.most_common())}
        (out_dir / "summary.json").write_text(json.dumps(summary,ensure_ascii=False,indent=2),encoding="utf-8")
        print(json.dumps(summary,ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
