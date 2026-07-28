"""Read-only report for Blender MOT actions and old/current skeleton rest data."""

import json
import math
import sys

import bpy
from mathutils import Matrix, Quaternion, Vector

from io_gbfr_blender_tools.Entities.ModelSkeleton import ModelSkeleton


def stored_rest(bone):
    position = bone.get("gbfr_rest_position")
    rotation = bone.get("gbfr_rest_quaternion")
    scale = bone.get("gbfr_rest_scale")
    if position is None or rotation is None or scale is None:
        return None
    return Matrix.LocRotScale(
        Vector(tuple(float(value) for value in position)),
        Quaternion(tuple(float(value) for value in rotation)),
        Vector(tuple(float(value) for value in scale)),
    )


def current_game_rest(bone):
    if bone.parent is not None:
        return bone.parent.matrix_local.inverted_safe() @ bone.matrix_local
    return Matrix.Rotation(-math.pi / 2.0, 4, "X") @ bone.matrix_local


def matrix_error(left, right):
    return max(
        abs(left[row][column] - right[row][column])
        for row in range(4)
        for column in range(4)
    )


skeleton_path = next(
    (value.split("=", 1)[1] for value in sys.argv if value.startswith("--skeleton=")),
    "",
)
skeleton_rest = {}
if skeleton_path:
    with open(skeleton_path, "rb") as stream:
        skeleton = ModelSkeleton.GetRootAs(bytearray(stream.read()), 0)
    for index in range(skeleton.BodyLength()):
        item = skeleton.Body(index)
        name = item.Name().decode("ascii")
        position = item.Position()
        rotation = item.Quat()
        scale = item.Scale()
        skeleton_rest[name] = Matrix.LocRotScale(
            Vector((position.X(), position.Y(), position.Z())),
            Quaternion((rotation.W(), rotation.X(), rotation.Y(), rotation.Z())),
            Vector((scale.X(), scale.Y(), scale.Z())),
        )


report = {
    "blend": bpy.data.filepath,
    "active_object": bpy.context.view_layer.objects.active.name
    if bpy.context.view_layer.objects.active else "",
    "actions": [],
    "armatures": [],
}
for action in bpy.data.actions:
    report["actions"].append(
        {
            "name": action.name,
            "frame_range": list(action.frame_range),
            "curves": len(action.fcurves),
            "properties": {
                key: action[key]
                for key in action.keys()
                if key.startswith("gbfr_mot_")
            },
        }
    )
for armature in (obj for obj in bpy.data.objects if obj.type == "ARMATURE"):
    changed = []
    unpack_errors = []
    comparable = 0
    for bone in armature.data.bones:
        old = stored_rest(bone)
        if old is None:
            continue
        comparable += 1
        error = matrix_error(old, current_game_rest(bone))
        if error > 1e-5:
            changed.append(
                {
                    "name": bone.name,
                    "bone_id": int(bone.get("gbfr_bone_id", -1)),
                    "max_error": error,
                }
            )
        if bone.name in skeleton_rest:
            unpack_error = matrix_error(skeleton_rest[bone.name], current_game_rest(bone))
            if unpack_error > 1e-5:
                unpack_errors.append(unpack_error)
    state = getattr(armature, "gbfr_animation", None)
    cache_key = state.cache_key if state else ""
    actions = []
    animation_entries = []
    if state:
        animation_entries = [
            {
                "name": entry.name,
                "path": entry.path,
                "source": entry.source_path,
                "template": entry.template_path,
                "unpack": entry.unpack_path,
                "action": entry.action_name,
                "edit_action": entry.edit_action_name,
            }
            for entry in state.animations
            if entry.action_name or entry.edit_action_name
        ]
    for action in bpy.data.actions:
        if action.get("gbfr_mot_session") == cache_key:
            actions.append(action.name)
    report["armatures"].append(
        {
            "object": armature.name,
            "model_id": state.model_id if state else "",
            "minfo": state.minfo_path if state else "",
            "cache_key": cache_key,
            "selected": armature.select_get(),
            "hidden": armature.hide_get(),
            "children": [child.name for child in armature.children],
            "comparable_bones": comparable,
            "changed_bones": sorted(changed, key=lambda item: item["bone_id"]),
            "unpack_compared_bones": sum(
                1 for bone in armature.data.bones if bone.name in skeleton_rest
            ),
            "unpack_changed_bones": len(unpack_errors),
            "unpack_max_error": max(unpack_errors, default=0.0),
            "actions": actions,
            "animation_entries": animation_entries,
        }
    )

if "--compact" in sys.argv:
    compact = {
        "blend": report["blend"],
        "active_object": report["active_object"],
        "actions": report["actions"],
        "armatures": [
            {
                key: value
                for key, value in armature.items()
                if key != "changed_bones"
            }
            | {
                "changed_bone_count": len(armature["changed_bones"]),
                "changed_bone_names": [
                    item["name"] for item in armature["changed_bones"]
                ],
            }
            for armature in report["armatures"]
        ],
    }
    print("GBFR_REPORT=" + json.dumps(compact, ensure_ascii=False, separators=(",", ":")))
else:
    json.dump(report, sys.stdout, ensure_ascii=False, indent=2)
    print()
