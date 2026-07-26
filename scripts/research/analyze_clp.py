#!/usr/bin/env python3
"""Decode and summarize GBFR CLP files for cloth authoring research."""

from __future__ import annotations

import argparse
import concurrent.futures
import csv
import json
import math
import re
import statistics
import subprocess
import sys
import xml.etree.ElementTree as ET
from collections import Counter, defaultdict
from pathlib import Path
from typing import Any, Iterable


SENTINEL = 4095
FILE_RE = re.compile(r"(?P<model>[^/\\]+)_0_(?P<group>\d+)_clp\.bxm(?:\.xml)?$", re.IGNORECASE)
LINK_FIELDS = ("noUp", "noDown", "noSide", "noPoly", "noFix")
NODE_FLOAT_FIELDS = (
    "rotLimit",
    "friction",
    "gravityBlendRate_",
    "originalRate_",
    "weight_",
    "thick_",
    "windForceArea_",
    "jointScale_",
    "axisAdjustRate_",
)


def text_value(element: ET.Element | None) -> str:
    return "" if element is None or element.text is None else element.text.strip()


def scalar(value: str) -> Any:
    if not value:
        return ""
    parts = value.split()
    if len(parts) > 1:
        try:
            return [float(part) for part in parts]
        except ValueError:
            return value
    try:
        return int(value)
    except ValueError:
        try:
            number = float(value)
            return 0.0 if number == 0.0 else number
        except ValueError:
            return value


def child_map(element: ET.Element) -> dict[str, Any]:
    return {child.tag: scalar(text_value(child)) for child in element}


def decode_one(tool: Path, source: Path, output: Path) -> tuple[Path, str | None]:
    if output.is_file() and output.stat().st_mtime_ns >= source.stat().st_mtime_ns:
        return output, None
    output.parent.mkdir(parents=True, exist_ok=True)
    completed = subprocess.run(
        [str(tool), "bxm-to-xml", "-i", str(source), "-o", str(output)],
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    if completed.returncode != 0 or not output.is_file():
        detail = (completed.stderr or completed.stdout).strip()
        return output, f"{source}: {detail or 'decoder failed'}"
    return output, None


def decode_tree(tool: Path, source_root: Path, cache_root: Path, workers: int) -> list[Path]:
    sources = sorted(source_root.rglob("*_clp.bxm"))
    jobs = [(source, cache_root / source.relative_to(source_root).with_suffix(".bxm.xml")) for source in sources]
    errors: list[str] = []
    outputs: list[Path] = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=workers) as executor:
        futures = [executor.submit(decode_one, tool, source, output) for source, output in jobs]
        for future in concurrent.futures.as_completed(futures):
            output, error = future.result()
            if error:
                errors.append(error)
            else:
                outputs.append(output)
    if errors:
        preview = "\n".join(errors[:20])
        raise RuntimeError(f"Failed to decode {len(errors)} CLP files:\n{preview}")
    return sorted(outputs)


def median(values: Iterable[float]) -> float | None:
    values = list(values)
    return statistics.median(values) if values else None


def finite_number(value: Any) -> float | None:
    if isinstance(value, (int, float)) and math.isfinite(value):
        return float(value)
    return None


def summarize_numbers(values: Iterable[Any]) -> dict[str, float] | None:
    numbers = [number for value in values if (number := finite_number(value)) is not None]
    if not numbers:
        return None
    return {
        "min": min(numbers),
        "median": median(numbers),
        "max": max(numbers),
    }


def topology(nodes: list[dict[str, Any]]) -> dict[str, Any]:
    ids = {int(node["no"]) for node in nodes}
    by_id = {int(node["no"]): node for node in nodes}
    roots = [node_id for node_id, node in by_id.items() if int(node.get("noUp", SENTINEL)) not in ids]
    tips = [node_id for node_id, node in by_id.items() if int(node.get("noDown", SENTINEL)) not in ids]

    depths: dict[int, int] = {}
    chains: list[list[int]] = []
    visited_down: set[int] = set()
    for root in roots:
        chain: list[int] = []
        current = root
        local: set[int] = set()
        while current in by_id and current not in local:
            local.add(current)
            visited_down.add(current)
            depths.setdefault(current, len(chain))
            chain.append(current)
            nxt = int(by_id[current].get("noDown", SENTINEL))
            if nxt not in by_id:
                break
            current = nxt
        chains.append(chain)

    adjacency: dict[int, set[int]] = {node_id: set() for node_id in ids}
    for node_id, node in by_id.items():
        for field in ("noUp", "noDown", "noSide", "noPoly"):
            target = int(node.get(field, SENTINEL))
            if target in ids and target != node_id:
                adjacency[node_id].add(target)
                adjacency[target].add(node_id)
    components: list[list[int]] = []
    unseen = set(ids)
    while unseen:
        start = min(unseen)
        stack = [start]
        component: list[int] = []
        unseen.remove(start)
        while stack:
            current = stack.pop()
            component.append(current)
            for target in adjacency[current]:
                if target in unseen:
                    unseen.remove(target)
                    stack.append(target)
        components.append(sorted(component))

    side_edges = sum(int(node.get("noSide", SENTINEL)) in ids for node in nodes)
    poly_edges = sum(int(node.get("noPoly", SENTINEL)) in ids for node in nodes)
    fixed_refs = [
        int(node["noFix"])
        for node in nodes
        if int(node.get("noFix", SENTINEL)) != SENTINEL
    ]
    branch_nodes = Counter(int(node.get("noUp", SENTINEL)) for node in nodes)
    branch_count = sum(count > 1 and node_id in ids for node_id, count in branch_nodes.items())
    chain_lengths = [len(chain) for chain in chains]
    file_order = [int(node["no"]) for node in nodes]
    chain_major_order = [node_id for chain in chains for node_id in chain]
    chain_by_node = {
        node_id: chain_index
        for chain_index, chain in enumerate(chains)
        for node_id in chain
    }
    horizontal_edges: set[tuple[int, int]] = set()
    for node_id, node in by_id.items():
        target = int(node.get("noSide", SENTINEL))
        if node_id not in chain_by_node or target not in chain_by_node:
            continue
        left = chain_by_node[node_id]
        right = chain_by_node[target]
        if left != right:
            horizontal_edges.add(tuple(sorted((left, right))))
    horizontal_adjacency: dict[int, set[int]] = defaultdict(set)
    for left, right in horizontal_edges:
        horizontal_adjacency[left].add(right)
        horizontal_adjacency[right].add(left)
    horizontal_components: list[dict[str, Any]] = []
    unseen_chains = set(horizontal_adjacency)
    while unseen_chains:
        start = min(unseen_chains)
        stack = [start]
        component: set[int] = set()
        unseen_chains.remove(start)
        while stack:
            current = stack.pop()
            component.add(current)
            for target in horizontal_adjacency[current]:
                if target in unseen_chains:
                    unseen_chains.remove(target)
                    stack.append(target)
        edge_count = sum(
            left in component and right in component
            for left, right in horizontal_edges
        )
        degrees = [len(horizontal_adjacency[chain] & component) for chain in component]
        horizontal_components.append({
            "chains": sorted(component),
            "chain_count": len(component),
            "edge_count": edge_count,
            "closed": len(component) >= 3 and edge_count == len(component) and all(degree == 2 for degree in degrees),
        })

    if not nodes:
        shape = "empty"
    elif len(nodes) == 1:
        shape = "point"
    elif side_edges or poly_edges:
        shape = "sheet_grid" if len(roots) >= 2 and branch_count == 0 else "linked_mixed"
    elif len(components) > 1 or len(roots) > 1:
        shape = "independent_chains"
    elif branch_count:
        shape = "branched_chain"
    else:
        shape = "single_chain"

    return {
        "shape": shape,
        "roots": sorted(roots),
        "tips": sorted(tips),
        "chain_count": len(chains),
        "chain_lengths": chain_lengths,
        "component_count": len(components),
        "component_sizes": sorted((len(component) for component in components), reverse=True),
        "side_edges": side_edges,
        "poly_edges": poly_edges,
        "fixed_ref_count": len(fixed_refs),
        "fixed_refs": sorted(set(fixed_refs)),
        "branch_count": branch_count,
        "file_order_is_chain_major": file_order == chain_major_order,
        "horizontal_components": horizontal_components,
        "unreached_by_down": sorted(ids - visited_down),
        "depths": {str(node_id): depth for node_id, depth in sorted(depths.items())},
    }


def parse_clp(path: Path) -> dict[str, Any]:
    root = ET.parse(path).getroot()
    if root.tag != "CLOTH":
        raise ValueError(f"{path}: expected CLOTH root, got {root.tag}")
    match = FILE_RE.search(path.name)
    header_element = root.find("CLOTH_HEADER")
    list_element = root.find("CLOTH_WK_LIST")
    if header_element is None or list_element is None:
        raise ValueError(f"{path}: missing CLOTH_HEADER or CLOTH_WK_LIST")
    header = child_map(header_element)
    nodes = [child_map(element) for element in list_element.findall("CLOTH_WK")]
    graph = topology(nodes)
    depths = {int(node_id): depth for node_id, depth in graph["depths"].items()}

    by_depth: dict[int, dict[str, Any]] = {}
    depth_groups: dict[int, list[dict[str, Any]]] = defaultdict(list)
    for node in nodes:
        if int(node["no"]) in depths:
            depth_groups[depths[int(node["no"])]].append(node)
    for depth, depth_nodes in sorted(depth_groups.items()):
        by_depth[depth] = {
            field: summary
            for field in NODE_FLOAT_FIELDS
            if (summary := summarize_numbers(node.get(field) for node in depth_nodes)) is not None
        }

    return {
        "path": str(path),
        "model": match.group("model") if match else path.stem,
        "group": int(match.group("group")) if match else int(header.get("id_", -1)),
        "header": header,
        "node_count": len(nodes),
        "topology": graph,
        "node_fields": {
            field: summary
            for field in NODE_FLOAT_FIELDS
            if (summary := summarize_numbers(node.get(field) for node in nodes)) is not None
        },
        "by_depth": by_depth,
        "nodes": nodes,
    }


def add_skeleton_checks(records: list[dict[str, Any]], data_root: Path, entities_root: Path) -> None:
    sys.path.insert(0, str(entities_root))
    try:
        from Entities.ModelSkeleton import ModelSkeleton  # type: ignore[import-not-found]
    finally:
        sys.path.pop(0)

    cache: dict[str, dict[int, int | None]] = {}
    for record in records:
        model = record["model"]
        if model not in cache:
            asset_type = re.match(r"[A-Za-z]+", model)
            if asset_type is None:
                cache[model] = {}
                continue
            skeleton_path = data_root / "model" / asset_type.group(0).lower() / model / f"{model}.skeleton"
            if not skeleton_path.is_file():
                cache[model] = {}
                continue
            skeleton = ModelSkeleton.GetRootAs(bytearray(skeleton_path.read_bytes()), 0)
            indexed: list[tuple[int, int]] = []
            for index in range(skeleton.BodyLength()):
                bone = skeleton.Body(index)
                name = bone.Name().decode("ascii")
                if re.fullmatch(r"_[0-9a-fA-F]{3}", name):
                    indexed.append((int(name[1:], 16), int(bone.ParentId())))
                else:
                    indexed.append((-1, int(bone.ParentId())))
            parents: dict[int, int | None] = {}
            for bone_id, parent_index in indexed:
                if bone_id < 0:
                    continue
                parent_id = indexed[parent_index][0] if 0 <= parent_index < len(indexed) else -1
                parents[bone_id] = parent_id if parent_id >= 0 else None
            cache[model] = parents

        parents = cache[model]
        ids = {int(node["no"]) for node in record["nodes"]}
        checked = 0
        matched = 0
        mismatches: list[dict[str, int | None]] = []
        for node in record["nodes"]:
            node_id = int(node["no"])
            up = int(node.get("noUp", SENTINEL))
            if up not in ids or node_id not in parents:
                continue
            checked += 1
            actual_parent = parents[node_id]
            if actual_parent == up:
                matched += 1
            else:
                mismatches.append({"node": node_id, "clp_up": up, "skeleton_parent": actual_parent})
        record["skeleton_check"] = {
            "checked_parent_edges": checked,
            "matched_parent_edges": matched,
            "mismatches": mismatches,
        }


def write_csv(path: Path, records: list[dict[str, Any]]) -> None:
    header_fields = sorted({field for record in records for field in record["header"]})
    columns = [
        "model", "group", "node_count", "shape", "chain_count", "chain_lengths",
        "component_count", "side_edges", "poly_edges", "fixed_ref_count", "path",
    ] + [f"header.{field}" for field in header_fields]
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8-sig") as stream:
        writer = csv.DictWriter(stream, fieldnames=columns)
        writer.writeheader()
        for record in records:
            graph = record["topology"]
            row = {
                "model": record["model"],
                "group": record["group"],
                "node_count": record["node_count"],
                "shape": graph["shape"],
                "chain_count": graph["chain_count"],
                "chain_lengths": ",".join(map(str, graph["chain_lengths"])),
                "component_count": graph["component_count"],
                "side_edges": graph["side_edges"],
                "poly_edges": graph["poly_edges"],
                "fixed_ref_count": graph["fixed_ref_count"],
                "path": record["path"],
            }
            row.update({f"header.{field}": value for field, value in record["header"].items()})
            writer.writerow(row)


def common_values(values: Iterable[Any], limit: int = 12) -> list[dict[str, Any]]:
    counter: Counter[str] = Counter()
    originals: dict[str, Any] = {}
    for value in values:
        if isinstance(value, float):
            value = round(value, 6)
        key = json.dumps(value, ensure_ascii=False, sort_keys=True)
        counter[key] += 1
        originals[key] = value
    return [
        {"value": originals[key], "count": count}
        for key, count in counter.most_common(limit)
    ]


def aggregate_records(records: list[dict[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {
        "file_count": len(records),
        "shape_counts": dict(Counter(record["topology"]["shape"] for record in records)),
        "integrity": Counter(),
        "by_shape": {},
    }
    all_header_fields = sorted({field for record in records for field in record["header"]})
    for record in records:
        nodes = record["nodes"]
        ids = {int(node["no"]) for node in nodes}
        by_id = {int(node["no"]): node for node in nodes}
        for node in nodes:
            node_id = int(node["no"])
            up = int(node.get("noUp", SENTINEL))
            down = int(node.get("noDown", SENTINEL))
            side = int(node.get("noSide", SENTINEL))
            poly = int(node.get("noPoly", SENTINEL))
            fixed = int(node.get("noFix", SENTINEL))
            if up in ids and int(by_id[up].get("noDown", SENTINEL)) != node_id:
                result["integrity"]["up_down_nonreciprocal"] += 1
            if down in ids and int(by_id[down].get("noUp", SENTINEL)) != node_id:
                result["integrity"]["down_up_nonreciprocal"] += 1
            for field, target in (("up", up), ("down", down), ("side", side), ("poly", poly)):
                if target != SENTINEL and target not in ids:
                    result["integrity"][f"{field}_external"] += 1
            if fixed != SENTINEL:
                result["integrity"]["fix_refs"] += 1
                result["integrity"]["fix_internal" if fixed in ids else "fix_external"] += 1
            if side == poly:
                if side != SENTINEL:
                    result["integrity"]["side_poly_same"] += 1
                    reciprocal = side in by_id and int(by_id[side].get("noSide", SENTINEL)) == node_id
                    result["integrity"]["side_reciprocal" if reciprocal else "side_one_way"] += 1
            else:
                result["integrity"]["side_poly_different"] += 1

        result["integrity"][
            "file_order_chain_major" if record["topology"]["file_order_is_chain_major"]
            else "file_order_not_chain_major"
        ] += 1
        for component in record["topology"]["horizontal_components"]:
            result["integrity"][
                "horizontal_components_closed" if component["closed"]
                else "horizontal_components_open_or_irregular"
            ] += 1
        skeleton_check = record.get("skeleton_check")
        if skeleton_check:
            result["integrity"]["skeleton_parent_edges_checked"] += skeleton_check["checked_parent_edges"]
            result["integrity"]["skeleton_parent_edges_matched"] += skeleton_check["matched_parent_edges"]
            result["integrity"]["skeleton_parent_edges_mismatched"] += len(skeleton_check["mismatches"])

    result["integrity"] = dict(result["integrity"])
    for shape in sorted(result["shape_counts"]):
        subset = [record for record in records if record["topology"]["shape"] == shape]
        shape_result: dict[str, Any] = {
            "file_count": len(subset),
            "topology": {},
            "headers": {},
            "node_fields": {},
            "root_to_tip": {},
            "depth_uniformity": {},
        }
        topology_values = {
            "node_count": [record["node_count"] for record in subset],
            "chain_count": [record["topology"]["chain_count"] for record in subset],
            "component_count": [record["topology"]["component_count"] for record in subset],
            "side_edges": [record["topology"]["side_edges"] for record in subset],
            "poly_edges": [record["topology"]["poly_edges"] for record in subset],
            "max_chain_length": [max(record["topology"]["chain_lengths"], default=0) for record in subset],
        }
        for field, values in topology_values.items():
            shape_result["topology"][field] = {
                "summary": summarize_numbers(values),
                "common": common_values(values),
            }
        for field in all_header_fields:
            values = [record["header"].get(field) for record in subset if field in record["header"]]
            shape_result["headers"][field] = {
                "summary": summarize_numbers(values),
                "common": common_values(values),
            }
        for field in NODE_FLOAT_FIELDS:
            values = [node.get(field) for record in subset for node in record["nodes"] if field in node]
            shape_result["node_fields"][field] = {
                "summary": summarize_numbers(values),
                "common": common_values(values),
            }
            roots: list[float] = []
            tips: list[float] = []
            deltas: list[float] = []
            for record in subset:
                depth_items = sorted((int(depth), fields) for depth, fields in record["by_depth"].items())
                if not depth_items:
                    continue
                root_summary = depth_items[0][1].get(field)
                tip_summary = depth_items[-1][1].get(field)
                if root_summary and tip_summary:
                    root = float(root_summary["median"])
                    tip = float(tip_summary["median"])
                    roots.append(root)
                    tips.append(tip)
                    deltas.append(tip - root)
            shape_result["root_to_tip"][field] = {
                "root": summarize_numbers(roots),
                "tip": summarize_numbers(tips),
                "delta": summarize_numbers(deltas),
                "trend_counts": {
                    "rising": sum(delta > 1e-6 for delta in deltas),
                    "flat": sum(abs(delta) <= 1e-6 for delta in deltas),
                    "falling": sum(delta < -1e-6 for delta in deltas),
                },
            }
        for field in (*NODE_FLOAT_FIELDS, "offset", "bAllowChangeScale_"):
            layer_count = 0
            uniform_count = 0
            for record in subset:
                depths = {int(node_id): int(depth) for node_id, depth in record["topology"]["depths"].items()}
                layers: dict[int, list[Any]] = defaultdict(list)
                for node in record["nodes"]:
                    node_id = int(node["no"])
                    if node_id in depths and field in node:
                        layers[depths[node_id]].append(node[field])
                for values in layers.values():
                    if len(values) < 2:
                        continue
                    layer_count += 1
                    encoded = {json.dumps(value, sort_keys=True) for value in values}
                    uniform_count += len(encoded) == 1
            shape_result["depth_uniformity"][field] = {
                "comparable_layers": layer_count,
                "uniform_layers": uniform_count,
                "rate": uniform_count / layer_count if layer_count else None,
            }
        result["by_shape"][shape] = shape_result
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("inputs", nargs="*", type=Path, help="CLP XML files or directories")
    parser.add_argument("--decode-root", type=Path, help="Raw game data root containing *_clp.bxm")
    parser.add_argument("--tool", type=Path, help="GBFRDataTools.exe used with --decode-root")
    parser.add_argument("--cache", type=Path, help="Decoded XML cache used with --decode-root")
    parser.add_argument("--workers", type=int, default=4)
    parser.add_argument("--json", type=Path)
    parser.add_argument("--csv", type=Path)
    parser.add_argument("--summary-json", type=Path)
    parser.add_argument("--skeleton-data-root", type=Path, help="Extracted data root containing model/* skeletons")
    parser.add_argument("--entities-root", type=Path, help="Directory containing the generated Entities package")
    args = parser.parse_args()

    xml_paths: set[Path] = set()
    if args.decode_root:
        if not args.tool or not args.cache:
            parser.error("--decode-root requires --tool and --cache")
        xml_paths.update(decode_tree(args.tool, args.decode_root, args.cache, max(1, args.workers)))
    for input_path in args.inputs:
        if input_path.is_dir():
            xml_paths.update(input_path.rglob("*_clp.bxm.xml"))
        elif input_path.is_file():
            xml_paths.add(input_path)
        else:
            parser.error(f"input does not exist: {input_path}")
    if not xml_paths:
        parser.error("no CLP XML inputs found")

    records = [parse_clp(path) for path in sorted(xml_paths)]
    if args.skeleton_data_root:
        if not args.entities_root:
            parser.error("--skeleton-data-root requires --entities-root")
        add_skeleton_checks(records, args.skeleton_data_root, args.entities_root)
    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(json.dumps(records, ensure_ascii=False, indent=2), encoding="utf-8")
    if args.csv:
        write_csv(args.csv, records)
    if args.summary_json:
        args.summary_json.parent.mkdir(parents=True, exist_ok=True)
        args.summary_json.write_text(
            json.dumps(aggregate_records(records), ensure_ascii=False, indent=2),
            encoding="utf-8",
        )
    shapes = Counter(record["topology"]["shape"] for record in records)
    print(json.dumps({"files": len(records), "shapes": shapes}, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        raise SystemExit(1)
