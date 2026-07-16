# 模型与骨架 Mod 制作流程

本文记录从游戏原始角色资源到 Blender 编辑、插件导出和最终 Mod 目录的完整流程。

## 1. 建立角色工作区

将玩家角色的 `.minfo` 拖到 `探索角色资源.bat`，例如：

```text
data/model/pl/pl1400/pl1400.minfo
```

玩家角色会自动关联同数字 ID 的：

- `pl`：玩家主体。
- `fp`：玩家面部/头部。
- `wp`：玩家武器。

`fn` 和 `np` 是其他独立 NPC 的资源，不会自动加入玩家工作区。

探索结果位于：

```text
explore_output/
  manifest.md
  workspace.json
  source/data/
  unpack/data/
  build/data/
```

注意：再次运行探索器会重建整个 `explore_output`。不要把 Blender 工程或长期编辑文件只保存在 `explore_output/source` 中；开始编辑前应复制工作区，或将模型工程放到独立目录。

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

导出时需要在插件中选择一个已经包含同名 `.minfo` 的文件夹。例如导出 `pl1400` 时，所选目录中必须先存在：

```text
工作目录/
  pl1400.minfo
```

插件会读取这份原始 `.minfo` 作为模型描述模板。导出完成后，它会自动在所选目录下创建 `_Exported_MInfo`，不需要用户提前创建该文件夹：

```text
工作目录/
  pl1400.minfo               原始模板
  _Exported_MInfo/
    pl1400.mmesh
    pl1400.minfo             合并 Blender 网格信息后的新文件
    pl1400.skeleton
    pl1400.json
```

如果导出位置和原始 `.minfo` 不在同一目录，插件的 MInfo 转换步骤会直接报错。

各文件职责：

| 文件 | 作用 | 游戏是否直接读取 |
|---|---|---|
| `.mmesh` | 顶点、索引、权重等网格数据 | 是 |
| `.minfo` | LOD、chunk、子网格、材质索引、骨骼权重索引和包围盒等模型描述 | 是 |
| `.skeleton` | 骨骼层级、名称与静止姿态 | 是 |
| `.json` | 导出后 `.minfo` 的人类可读调试信息合集 | 否 |

### 导出 JSON 的实际作用

`pl1400.json` 不是游戏解包资源，也与 `.mmat` 封包无关。它是 Blender 插件生成和重建 `.minfo` 时使用的人类可读中间态，并在导出目录中保留下来方便检查。

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
  -> 从所选目录读取同名原始 minfo
  -> flatc 将原始 minfo 解码为 JSON
  -> 用 Blender 导出的网格布局替换原 minfo 对应字段
  -> flatc 将合并后的 JSON 编码为新 minfo
  -> 保留最终 JSON 供人工检查
```

因此：

- 游戏最终只需要二进制 `.minfo`，不需要这个 JSON。
- JSON 可以用于检查 chunk、材质索引、子网格和骨骼映射是否合理。
- 删除 JSON 不影响最终 Mod，但保留它有助于以后排错。
- `.mmat` 是独立材质配置，负责着色器参数和贴图引用，不会被这个 JSON 自动修改。

## 5. 最终 Mod 路径

插件导出完成后，将整个 `_Exported_MInfo` 文件夹拖入 `GBFR编辑封包工具.bat` 打开的窗口。构建器会忽略调试 JSON，并按文件名把三个二进制文件覆盖到当前工作区：

```text
unpack/data/model/pl/pl1400/pl1400.minfo
unpack/data/model/pl/pl1400/pl1400.skeleton
unpack/data/model_streaming/lod0/pl1400.mmesh
```

构建列表会将它们标记为已修改并自动勾选。需要撤销某个导出文件时，使用该行的“恢复”；构建后文件会恢复到游戏原始路径，而不是原样复制 `_Exported_MInfo` 文件夹。

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
_Exported_MInfo/pl1400.json
data/model/pl/pl1400/pl1400.mmesh
```

第二个路径中的 `.mmesh` 只是 Blender 同目录导入副本；正式 Mod 网格应放在 `data/model_streaming/lod0/`。

## 6. 材质与贴图何时需要一起修改

仅修改顶点、权重或现有骨架，并保持材质槽结构不变时，通常不需要修改 `.mmat`。

以下情况才需要额外构建材质或贴图：

- 修改贴图内容：编辑 `unpack/data/texture/.../*.dds`，再用构建器封回 `.texture`。
- 将 Granite 贴图改为普通贴图：编辑或保留 `unpack/data/granite/.../*.dds`，在构建器中勾选对应“新建贴图”，并删除 mmat 条目中的 `A4`。
- 修改贴图引用或 Granite 映射：编辑对应的 `*.mmat.json`。
- 新增、删除或重新排序材质槽：必须同时确认 `.minfo` 的 `chunks[].material_id`、`materials[]` 与 `vars/*.mmat` 是否仍然对应。

`.minfo` JSON 中的 `materials[]` 只是模型侧的材质哈希和索引信息，不等同于完整 `.mmat` 内容。

## 7. 导出后检查清单

- `_Exported_MInfo` 中存在新的 `.mmesh`、`.minfo` 和 `.skeleton`。
- `.json` 中 `vertex_count`、`poly_count_x3` 与预期模型规模相符。
- `chunks[].material_id` 没有超出 `materials[]` 范围。
- `sub_meshes` 名称、数量和包围盒合理。
- 修改骨架时，`bones_to_weight_indices` 与实际权重骨骼一致。
- 最终 Mod 中 `.mmesh` 位于 `model_streaming/lod0`。
- 最终 Mod 不包含 `_Exported_MInfo` 文件夹或调试 JSON。
- 游戏内检查远近距离 LOD、动作变形、面部、武器、阴影和材质显示。
