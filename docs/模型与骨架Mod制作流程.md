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

## 2. 从工作区导入 Blender

在 Blender 插件中选择工作区 `unpack` 或 `source` 中已登记的 `.minfo`：

```text
unpack/data/model/pl/pl1400/pl1400.minfo
```

插件会通过同级 `workspace.json` 自动找到：

```text
unpack/data/model/pl/pl1400/pl1400.skeleton
unpack/data/model_streaming/lod0/pl1400.mmesh
unpack/data/model_streaming/lod1/pl1400.mmesh
unpack/data/model_streaming/lod2/pl1400.mmesh
unpack/data/model_streaming/lod3/pl1400.mmesh
```

实际层级取决于原模型，存在 `shadowlod#` 时也会一起导入。Blender 中的结构为：

```text
pl1400                 <- 模型根对象；有骨架时就是 Armature
  lod0                 <- LOD 空对象
    body               <- 一个或多个 Mesh
    equipment
  lod1
    body
    equipment
```

不要再把 LOD0 `.mmesh` 手工复制到 `.minfo` 隔壁。工作区只认 `workspace.json/ModelFiles` 登记的正式路径；每个 minfo 会建立独立 Collection，会话之间的材质、动画、Cloth 和导出目标互不共享。

## 3. Blender 编辑注意事项

- 最终网格必须为三角面。
- 不要随意调整材质槽顺序；chunk 使用 `material_id` 引用材质索引。
- 简单修改模型时，尽量保留原材质对应的至少一个面。
- 顶点权重需遵守插件和游戏的骨骼影响数量限制，并在导出前归一化。
- 修改已有骨骼的姿态、层级或名称，比单纯修改网格风险更高。
- 新增、删除或重排骨骼后，需要重点检查权重索引、骨骼包围盒、动画兼容和运行时剔除。

## 4. 插件导出结果

在当前 minfo 会话中点击“导出到工作区”，然后选择目标工作区的 `workspace.json`。导出界面会显示当前模型 ID，以及将覆盖的 `.minfo`、可选 `.skeleton` 和全部 LOD `.mmesh`；它不会写入 `build`。

```text
workspace.json
unpack/data/model/pl/pl1400/pl1400.minfo
unpack/data/model/pl/pl1400/pl1400.skeleton
unpack/data/model_streaming/lod0/pl1400.mmesh
unpack/data/model_streaming/lod1/pl1400.mmesh
unpack/data/model_streaming/lod2/pl1400.mmesh
unpack/data/model_streaming/lod3/pl1400.mmesh
```

插件复制当前会话的完整模型层级到临时场景，由 v2 FlatBuffers 构建器直接生成二进制。只有全部登记输出都成功后才原子替换目标，因此不会破坏当前 Blender 场景，也不会创建 `_Exported_MInfo`、调试 JSON 或调用 `flatc.exe`。所选工作区必须登记同一模型 ID，且所有输出都必须位于该工作区 `unpack` 内。

### 融合骨架后的稳定顺序

Blender 会维护自己的骨架层级顺序。即使融合操作没有删除或移动任何原骨，只是把新骨挂到已有父骨，退出编辑模式后，新骨也可能被插入 `Armature.data.bones` 的旧骨序列中间。这个顺序适合 Blender 层级求值，但不能直接当作游戏 skeleton 索引；否则 cloth、SOP、动作和权重可能引用错误骨骼，严重时会在游戏载入模型时崩溃。

工作区导出会读取 `source` 中的原 `.skeleton`，按骨名建立稳定的二进制顺序：

- source 原骨骼保持原索引和顺序。
- 融合新增骨骼统一追加到全部原骨之后。
- 每根骨的 `ParentId` 按稳定顺序重新计算，父子关系不会因为追加排序而丢失。
- `.minfo` 的 deform bone 表与 `.mmesh` 权重索引使用相同映射。

因此不需要为了排序重建 Armature，也不需要修改通用骨架融合工具。已经完成融合的 Blender 文件可以直接重新导出。必须保留 source 中的全部原骨名和原父级；不要为了清理旧头发等部件删除旧骨，cloth、SOP 或动作仍可能按固定索引引用它们。缺少原骨、重复导出骨名或改变原父级会使严格骨架检查或导出失败。

已用 342 根原骨加 496 根新增骨的样本回归验证：Blender 内部出现中间插入后，导出的前 342 根名称和 `ParentId` 仍与 source 逐项一致，新增骨从索引 342 开始。

官方 v2 导出器会把 `.minfo`、`.skeleton` 和全部 `.mmesh` 作为同一组结果生成；不要只手工复制新的 `.mmesh` 去搭配旧的 `.minfo/.skeleton`。其第二组骨骼索引/权重、顶点色和 UV1 属于可选顶点缓冲，长度可以小于该 LOD 的总顶点数；C++ 预览器按缓冲实际覆盖的顶点读取，未覆盖部分使用默认值。正式 `.mmesh` 路径始终是 `unpack/data/model_streaming/lod#/` 或 `shadowlod#/`，放到 `.minfo` 同目录的副本不会被工作区预览使用。

各文件职责：

| 文件 | 作用 | 游戏是否直接读取 |
|---|---|---|
| `.mmesh` | 顶点、索引、权重等网格数据 | 是 |
| `.minfo` | LOD、chunk、子网格、材质索引、骨骼权重索引和包围盒等模型描述 | 是 |
| `.skeleton` | 骨骼层级、名称与静止姿态 | 是 |

## 5. 最终 Mod 路径

插件导出完成后，全部模型二进制已位于当前工作区的 `unpack` 对应路径，不需要再次复制：

```text
unpack/data/model/pl/pl1400/pl1400.minfo
unpack/data/model/pl/pl1400/pl1400.skeleton
unpack/data/model_streaming/lod0/pl1400.mmesh
unpack/data/model_streaming/lod1/pl1400.mmesh
unpack/data/model_streaming/lod2/pl1400.mmesh
unpack/data/model_streaming/lod3/pl1400.mmesh
```

返回编辑器点击“刷新”，各项会按 SHA-256 标记为已修改。点击具体 `lod#` 或 `shadowlod#` `.mmesh` 可切换对应层级预览；选择 minfo/skeleton 时默认显示 `lod0`。选中需要发布的模型项后，在 Inspector 使用“从 unpack 复制到 build”；需要撤销时使用“从 source 恢复 unpack”。`build` 是最终 Mod 输出，不参与预览。

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
      lod1/
        pl1400.mmesh
      lod2/
        pl1400.mmesh
      lod3/
        pl1400.mmesh
```

不要把 `.mmesh` 放到 `data/model/...`。正式网格必须保持 `workspace.json` 登记的 `data/model_streaming/lod#` 或 `shadowlod#` 路径。

## 6. 材质与贴图何时需要一起修改

仅修改顶点、权重或现有骨架，并保持材质槽结构不变时，通常不需要修改 `.mmat`。

以下情况在完整 Mod 流程中需要同时修改材质或贴图；当前仓库只负责保留和预览这些中间态，反向编码能力见本节末尾：

- 修改贴图内容：编辑 `unpack/data/texture/.../*.dds`，最终输出需要编码为对应 `.texture`。
- 将 Granite 贴图改为普通贴图：最终输出需要把 `unpack/data/granite/.../*.dds` 编码为普通 `.texture`，并删除 mmat 条目中的 `A4`。“新贴图”构建会在封装前自动垂直翻转 DDS 的全部 mip，不要预先手动翻转源 DDS。
- 修改贴图引用或 Granite 映射：编辑对应的 `*.mmat.json`。
- 新增、删除或重新排序材质槽：必须同时确认 `.minfo` 的 `chunks[].material_id`、`materials[]` 与 `vars/*.mmat` 是否仍然对应。

`.minfo` JSON 中的 `materials[]` 只是模型侧的材质哈希和索引信息，不等同于完整 `.mmat` 内容。

贴图、UI-image 与 mmat 使用编辑器各自的构建操作写入 `build`；模型导出不会自动改写 `.mmat` 或贴图内容。

## 7. 导出后检查清单

- 导出界面显示的全部 `unpack` 目标与当前模型 ID、工作区一致。
- Root 下只放需要导出的 `lod#`/`shadowlod#`，每层可包含一个或多个 Mesh。
- 每个 Mesh 最多有 2 个 UV；顶点权重最多 8 个且已归一化。
- 材质 `MaterialID` 在所有 LOD 中保持一致，没有空材质槽或越界引用。
- 修改骨架时保留游戏骨骼原名/`gbfr_bone_id`，并检查动画、SOP 与权重组。
- 融合时保留全部原骨和原父级；新增骨由导出器追加到 source 骨骼之后。
- `.minfo`、`.skeleton` 和全部 `.mmesh` 必须来自同一次导出并整组进入最终 Mod。
- 最终 Mod 中每个 `.mmesh` 位于对应的 `model_streaming/lod#` 或 `shadowlod#`。
- 游戏内检查远近距离 LOD、动作变形、面部、武器、阴影和材质显示。

## 附录：位移锚点与运行时对齐

当前奶刀样本确认 `_000`（hips）和 `_005`（head/neck）都属于不能随意移动的位移锚点。它们的 source rest transform 必须保留；替换模型时应调整网格、权重和普通子骨，而不是把这两个骨骼吸附到另一套骨架的位置。游戏动画和约束可能会对它们写入平移，所以 Blender 静态视图对齐并不能保证游戏内接缝和脚部高度正确。

预览器、材质和游戏内表面噪波的后续问题统一记录在[模型骨架、预览器与材质问题记录](模型骨架预览与材质问题记录.md)。
