"""Rebase edited MOT files from an old skeleton rest to a new unpack rest.

Run with Blender's Python because the conversion uses mathutils::Matrix semantics:

    blender --background --factory-startup --python rebase_mot_rest.py -- \
        --source-skeleton ... --unpack-skeleton ... \
        --source-dir ... --unpack-dir ... --output-dir ...

Only MOT files whose unpack hash differs from the same-named source file are
converted. Inputs are never overwritten by this script.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import sys

from mathutils import Euler, Matrix, Quaternion, Vector

from io_gbfr_blender_tools.Entities.ModelSkeleton import ModelSkeleton
from io_gbfr_blender_tools.gbfr_animation import load_mot, write_mot_template_atomic


SUPPORTED_PROPERTIES = frozenset({0, 1, 2, 3, 4, 5, 7, 8, 9})


def parse_args():
    arguments = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    parser = argparse.ArgumentParser()
    parser.add_argument("--source-skeleton", type=Path, required=True)
    parser.add_argument("--unpack-skeleton", type=Path, required=True)
    parser.add_argument("--source-dir", type=Path, required=True)
    parser.add_argument("--unpack-dir", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--name", action="append", default=[])
    return parser.parse_args(arguments)


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def skeleton_rest(path):
    skeleton = ModelSkeleton.GetRootAs(bytearray(path.read_bytes()), 0)
    result = {}
    for index in range(skeleton.BodyLength()):
        item = skeleton.Body(index)
        name = item.Name().decode("ascii")
        if not name.startswith("_"):
            continue
        try:
            bone_id = int(name[1:], 16)
        except ValueError:
            continue
        position = item.Position()
        rotation = item.Quat()
        scale = item.Scale()
        location = (position.X(), position.Y(), position.Z())
        quaternion = (rotation.W(), rotation.X(), rotation.Y(), rotation.Z())
        scaling = (scale.X(), scale.Y(), scale.Z())
        matrix = Matrix.LocRotScale(
            Vector(location), Quaternion(quaternion), Vector(scaling),
        )
        result[bone_id] = {
            "position": location,
            "rotation": quaternion,
            "scale": scaling,
            "matrix": matrix,
            "inverse": matrix.inverted_safe(),
        }
    return result


def quaternion_to_euler(rotation):
    w, x, y, z = rotation
    import math
    return [
        math.atan2(2.0 * (w * x + y * z), 1.0 - 2.0 * (x * x + y * y)),
        math.asin(max(-1.0, min(1.0, 2.0 * (w * y - z * x)))),
        math.atan2(2.0 * (w * z + x * y), 1.0 - 2.0 * (y * y + z * z)),
    ]


def local_matrix(rest, property_samples, frame):
    position = list(rest["position"])
    rotation = quaternion_to_euler(rest["rotation"])
    scale = list(rest["scale"])
    for prop, samples in property_samples.items():
        value = samples[frame]
        if prop <= 2:
            position[prop] = value
        elif prop <= 5:
            rotation[prop - 3] = value
        else:
            scale[prop - 7] = value
    return Matrix.LocRotScale(
        Vector(position), Euler(rotation, "XYZ").to_quaternion(), Vector(scale),
    )


def matrix_error(left, right):
    return max(
        abs(left[row][column] - right[row][column])
        for row in range(4)
        for column in range(4)
    )


def trs_projection(matrix):
    location, rotation, scale = matrix.decompose()
    rotation.normalize()
    return Matrix.LocRotScale(location, rotation, scale), location, rotation, scale


def convert_clip(clip, source_clip, old_rest, new_rest):
    source_contract = [
        (track.bone_id, track.property, track.unknown) for track in source_clip.tracks
    ]
    output_contract = [
        (track.bone_id, track.property, track.unknown) for track in clip.tracks
    ]
    if (
        source_clip.frame_count != clip.frame_count
        or source_contract != output_contract
    ):
        raise ValueError(f"source template contract differs: {clip.path.name}")
    sampled = [
        [track.sample(frame) for frame in range(clip.frame_count)]
        for track in clip.tracks
    ]
    source_sampled = [
        [track.sample(frame) for frame in range(source_clip.frame_count)]
        for track in source_clip.tracks
    ]
    tracks_by_bone = {}
    source_tracks_by_bone = {}
    track_indices = {}
    for index, track in enumerate(clip.tracks):
        bone_id = 0x900 if track.bone_id == -1 else int(track.bone_id)
        prop = int(track.property)
        if (
            prop not in SUPPORTED_PROPERTIES
            or bone_id not in old_rest
            or bone_id not in new_rest
        ):
            continue
        key = (bone_id, prop)
        if key in track_indices:
            raise ValueError(f"duplicate MOT track {bone_id:#x}.{prop}")
        track_indices[key] = index
        tracks_by_bone.setdefault(bone_id, {})[prop] = sampled[index]
        source_tracks_by_bone.setdefault(bone_id, {})[prop] = source_sampled[index]

    maximum_error = 0.0
    worst = None
    maximum_action_projection = 0.0
    maximum_output_projection = 0.0
    complete_bones = 0
    incomplete_bones = 0
    for bone_id, property_samples in tracks_by_bone.items():
        if set(property_samples) == SUPPORTED_PROPERTIES:
            complete_bones += 1
        else:
            incomplete_bones += 1
        previous_euler = None
        converted = {}
        targets = []
        for frame in range(clip.frame_count):
            source_local = local_matrix(
                old_rest[bone_id], source_tracks_by_bone[bone_id], frame,
            )
            exact_source_basis = old_rest[bone_id]["inverse"] @ source_local
            location, rotation, scale = exact_source_basis.decompose()
            projected_source_basis = Matrix.LocRotScale(location, rotation, scale)
            edited_local = local_matrix(old_rest[bone_id], property_samples, frame)
            edit_delta = (
                exact_source_basis.inverted_safe()
                @ old_rest[bone_id]["inverse"]
                @ edited_local
            )
            recovered_action = projected_source_basis @ edit_delta
            projected_action, _action_location, _action_rotation, _action_scale = (
                trs_projection(recovered_action)
            )
            maximum_action_projection = max(
                maximum_action_projection,
                matrix_error(recovered_action, projected_action),
            )
            raw_target = new_rest[bone_id]["matrix"] @ projected_action
            target, location, rotation, scale = trs_projection(raw_target)
            maximum_output_projection = max(
                maximum_output_projection, matrix_error(raw_target, target),
            )
            targets.append(target)
            euler = (
                rotation.to_euler("XYZ", previous_euler)
                if previous_euler is not None else rotation.to_euler("XYZ")
            )
            previous_euler = euler.copy()
            values = (*location, *euler, *scale)
            for prop in property_samples:
                value_index = prop if prop <= 5 else prop - 1
                converted.setdefault(prop, []).append(float(values[value_index]))
        for prop, values in converted.items():
            sampled[track_indices[(bone_id, prop)]] = values

        for frame, target in enumerate(targets):
            migrated_properties = {
                prop: converted[prop] for prop in property_samples
            }
            actual = local_matrix(new_rest[bone_id], migrated_properties, frame)
            error = matrix_error(target, actual)
            if error > maximum_error:
                maximum_error = error
                worst = {
                    "bone_id": bone_id,
                    "frame": frame,
                    "error": error,
                    "properties": sorted(property_samples),
                    "target": [list(row) for row in target],
                    "actual": [list(row) for row in actual],
                }

    return tuple(tuple(values) for values in sampled), {
        "converted_bones": len(tracks_by_bone),
        "complete_bones": complete_bones,
        "incomplete_bones": incomplete_bones,
        "maximum_pose_error": maximum_error,
        "worst_pose": worst,
        "maximum_action_projection": maximum_action_projection,
        "maximum_output_projection": maximum_output_projection,
    }


def main():
    args = parse_args()
    old_rest = skeleton_rest(args.source_skeleton.resolve())
    new_rest = skeleton_rest(args.unpack_skeleton.resolve())
    args.output_dir.mkdir(parents=True, exist_ok=True)
    records = []
    for unpack_path in sorted(args.unpack_dir.glob("*.mot"), key=lambda path: path.name.casefold()):
        if args.name and unpack_path.name not in args.name:
            continue
        source_path = args.source_dir / unpack_path.name
        if not source_path.is_file() or sha256(source_path) == sha256(unpack_path):
            continue
        clip = load_mot(unpack_path)
        source_clip = load_mot(source_path)
        samples, stats = convert_clip(clip, source_clip, old_rest, new_rest)
        output_path = args.output_dir / unpack_path.name
        write_mot_template_atomic(clip, samples, output_path)
        reparsed = load_mot(output_path)
        if (
            reparsed.version != clip.version
            or reparsed.flags != clip.flags
            or reparsed.frame_count != clip.frame_count
            or reparsed.unknown != clip.unknown
            or reparsed.name != clip.name
            or len(reparsed.tracks) != len(clip.tracks)
        ):
            raise ValueError(f"roundtrip contract changed: {unpack_path.name}")
        records.append({
            "name": unpack_path.name,
            "input_sha256": sha256(unpack_path),
            "output_sha256": sha256(output_path),
            "frames": clip.frame_count,
            "tracks": len(clip.tracks),
            **stats,
        })
        print(
            f"{unpack_path.name}: {clip.frame_count} frames, {len(clip.tracks)} tracks, "
            f"{stats['converted_bones']} bones, pose error {stats['maximum_pose_error']:.6g}"
        )
    manifest = {
        "source_skeleton": str(args.source_skeleton.resolve()),
        "source_skeleton_sha256": sha256(args.source_skeleton),
        "unpack_skeleton": str(args.unpack_skeleton.resolve()),
        "unpack_skeleton_sha256": sha256(args.unpack_skeleton),
        "files": records,
    }
    (args.output_dir / "manifest.json").write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8",
    )
    print(f"converted {len(records)} MOT files into {args.output_dir.resolve()}")


if __name__ == "__main__":
    main()
