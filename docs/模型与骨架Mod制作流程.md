# 模型与骨架 Mod 制作流程

> 角色 `_aXX` deform/corrective 骨由原始 `.sop` 在 `.mot` 之后驱动，不是普通动画骨。修改骨架前先阅读 [SOP 骨骼后处理与 Deform 骨](formats/SOP骨骼后处理与Deform骨.md)，并保留 SOP 引用的原始骨名。

本文记录从游戏原始角色资源到 Blender 编辑、插件导出和最终 Mod 目录的完整流程。

## 1. 建立角色工作区

双击根目录 `build.bat` 启动编辑器，在开始页选择玩家角色 `.minfo`，并指定本次工作区的输出目录，例如：

```text
data/model/pl/pl1400/pl1400.minfo
```

玩家角色会自动关联同数字 ID 的：

- `pl`：玩家主体。
- `fp`：玩家面部/头部。
- `wp`：玩家武器。

`fn` 和 `np` 是其他独立 NPC 的资源，不会自动加入玩家工作区。

输出目录留空时使用根目录的 `explore_output`。指定目录后，生成的工作区位于：

```text
<所选工作区>/
  manifest.md
  workspace.json
  source/data/
  unpack/data/
  build/data/
```

注意：再次从 minfo 生成会重建整个所选输出目录。需要同时保留多个角色或 Mod 方案时，应分别选择独立目录；不要把 Blender 工程或其他无关文件放进会被重建的工作区目录。

## 2. 准备 Blender 导入文件

游戏的正式网格位于：

```text
data/model_streaming/lod0/pl1400.mmesh
```

而模型描述和骨架位于：

```text
data/model/pl/pl1400/pl1400.minfo
data/model/pl/pl1400/pl1400.skeleton
```

当前 Blender 插件导入时要求 `.mmesh` 与 `.minfo` 放在同一目录，因此需要临时复制：

```text
工作目录/
  pl1400.minfo
  pl1400.mmesh       <- 从 model_streaming/lod0 临时复制
  pl1400.skeleton
```

这份同目录 `.mmesh` 只是 Blender 导入辅助副本，不是游戏原始目录结构。角色探索器会静默忽略此类副本，正式网格始终以 `model_streaming/lod*` 为准。

## 3. Blender 编辑注意事项

- 最终网格必须为三角面。
- 不要随意调整材质槽顺序；chunk 使用 `material_id` 引用材质索引。
- 简单修改模型时，尽量保留原材质对应的至少一个面。
- 顶点权重需遵守插件和游戏的骨骼影响数量限制，并在导出前归一化。
- 修改已有骨骼的姿态、层级或名称，比单纯修改网格风险更高。
- 新增、删除或重排骨骼后，需要重点检查权重索引、骨骼包围盒、动画兼容和运行时剔除。

## 4. 插件导出结果

在当前 minfo 会话中点击“导出到工作区”，然后选择目标工作区的 `workspace.json`。导出界面会在确认前显示当前模型 ID、即将覆盖的三条 `unpack` 路径、调试 JSON 路径，并明确提示不会写入 `build`。

```text
workspace.json
unpack/data/model/pl/pl1400/pl1400.minfo
unpack/data/model/pl/pl1400/pl1400.skeleton
unpack/data/model_streaming/lod0/pl1400.mmesh
.gbfr/exports/pl1400.json
```

插件按当前会话的模型 ID 查询 `workspace.json/ModelFiles`，使用现有 `unpack` minfo（缺失时回退 `source`）作为描述模板，在临时目录完成全部转换。只有 `.minfo/.skeleton/.mmesh/.json` 都生成成功后才替换目标，因此不会再创建或要求用户管理 `_Exported_MInfo`。所选工作区必须登记同一模型 ID，且三个输出目标必须位于该工作区 `unpack` 内，否则导出会拒绝执行。

各文件职责：

| 文件 | 作用 | 游戏是否直接读取 |
|---|---|---|
| `.mmesh` | 顶点、索引、权重等网格数据 | 是 |
| `.minfo` | LOD、chunk、子网格、材质索引、骨骼权重索引和包围盒等模型描述 | 是 |
| `.skeleton` | 骨骼层级、名称与静止姿态 | 是 |
| `.json` | 导出后 `.minfo` 的人类可读调试信息合集 | 否 |

### 导出 JSON 的实际作用

`pl1400.json` 不是游戏解包资源，也与 `.mmat` 封包无关。它是 Blender 插件生成和重建 `.minfo` 时使用的人类可读中间态，导出后保存在工作区 `.gbfr/exports/pl1400.json` 方便检查。

它主要包含：

- `lods[].mesh_buffers`：`.mmesh` 各缓冲区偏移和大小。
- `lods[].chunks`：索引范围、子网格 ID 和材质 ID。
- `vertex_count` / `poly_count_x3`：顶点和三角形索引数量。
- `sub_meshes`：子网格名称与包围盒。
- `materials`：`.minfo` 中的材质名称哈希和标志。
- `bones_to_weight_indices`：骨骼到权重索引的映射。
- `deform_bone_boundary_box`：变形骨骼包围盒。
- 其他从原始 `.minfo` 保留下来的模型参数。

插件内部流程可以概括为：

```text
Blender 网格
  -> 生成新的 mmesh 与网格布局 JSON
  -> 从 workspace.json 定位当前模型的 minfo 模板
  -> flatc 将原始 minfo 解码为 JSON
  -> 用 Blender 导出的网格布局替换原 minfo 对应字段
  -> flatc 将合并后的 JSON 编码为新 minfo
  -> 原子替换 unpack 中登记的三个二进制文件
  -> 将最终 JSON 保存到 .gbfr/exports
```

因此：

- 游戏最终只需要二进制 `.minfo`，不需要这个 JSON。
- JSON 可以用于检查 chunk、材质索引、子网格和骨骼映射是否合理。
- 删除 JSON 不影响最终 Mod，但保留它有助于以后排错。
- `.mmat` 是独立材质配置，负责着色器参数和贴图引用，不会被这个 JSON 自动修改。

## 5. 最终 Mod 路径

插件导出完成后，三个二进制文件已经位于当前工作区的 `unpack` 对应路径，不需要再次复制：

```text
unpack/data/model/pl/pl1400/pl1400.minfo
unpack/data/model/pl/pl1400/pl1400.skeleton
unpack/data/model_streaming/lod0/pl1400.mmesh
```

返回编辑器点击“刷新”，三项会按 SHA-256 标记为已修改，预览器也会从 `unpack` 重新加载它们。选中每个模型项后，在 Inspector 使用“从 unpack 复制到 build”；需要撤销时使用“从 source 恢复 unpack”。`build` 是最终 Mod 输出，不参与预览。

```text
Mod目录/
  data/
    model/
      pl/
        pl1400/
          pl1400.minfo
          pl1400.skeleton
    model_streaming/
      lod0/
        pl1400.mmesh
```

不要将下列文件放进最终 Mod：

```text
.gbfr/exports/pl1400.json
data/model/pl/pl1400/pl1400.mmesh
```

第二个路径中的 `.mmesh` 只是 Blender 同目录导入副本；正式 Mod 网格应放在 `data/model_streaming/lod0/`。

## 6. 材质与贴图何时需要一起修改

仅修改顶点、权重或现有骨架，并保持材质槽结构不变时，通常不需要修改 `.mmat`。

以下情况在完整 Mod 流程中需要同时修改材质或贴图；当前仓库只负责保留和预览这些中间态，反向编码能力见本节末尾：

- 修改贴图内容：编辑 `unpack/data/texture/.../*.dds`，最终输出需要编码为对应 `.texture`。
- 将 Granite 贴图改为普通贴图：最终输出需要把 `unpack/data/granite/.../*.dds` 编码为普通 `.texture`，并删除 mmat 条目中的 `A4`。
- 修改贴图引用或 Granite 映射：编辑对应的 `*.mmat.json`。
- 新增、删除或重新排序材质槽：必须同时确认 `.minfo` 的 `chunks[].material_id`、`materials[]` 与 `vars/*.mmat` 是否仍然对应。

`.minfo` JSON 中的 `materials[]` 只是模型侧的材质哈希和索引信息，不等同于完整 `.mmat` 内容。

> 当前 C++ 版本尚未实现 WTB `.texture` 和 mmat JSON 的反向编码。因此涉及贴图或材质的 Mod 目前不能只依靠本仓库生成完整输出；编辑器会保留中间态供预览，但不会假装已构建成功。

## 7. 导出后检查清单

- 导出界面显示的三个 `unpack` 目标与当前模型 ID、工作区一致。
- `.gbfr/exports/<模型ID>.json` 中 `vertex_count`、`poly_count_x3` 与预期模型规模相符。
- `chunks[].material_id` 没有超出 `materials[]` 范围。
- `sub_meshes` 名称、数量和包围盒合理。
- 修改骨架时，`bones_to_weight_indices` 与实际权重骨骼一致。
- 最终 Mod 中 `.mmesh` 位于 `model_streaming/lod0`。
- 最终 Mod 不包含 `.gbfr` 或调试 JSON。
- 游戏内检查远近距离 LOD、动作变形、面部、武器、阴影和材质显示。
