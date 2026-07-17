# GBFR Modtools

新的 C++/Dear ImGui 编辑器已经推进到 M5。构建命令、使用方式、原生覆盖范围与 PowerShell 兼容边界见 [CPP编辑器构建与迁移.md](CPP编辑器构建与迁移.md)，总体架构和格式约束见 [CPP编辑器目标与架构.md](CPP编辑器目标与架构.md)。

双击 `build.bat` 进入 C++ 编辑器开始页：第一行选择游戏原始 `.minfo` 并抽取生成 `explore_output`，第二行选择已有 `workspace.json` 并进入编辑模式。选择 minfo 会完整替换旧 `explore_output`，执行前会确认。

用于建立《碧蓝幻想：Relink》角色资源工作区，并把编辑后的 DDS 与 mmat JSON 构建为可直接安装的 Mod 文件。

模型与骨架的 Blender 编辑、导出和安装流程见 [`模型与骨架Mod制作流程.md`](模型与骨架Mod制作流程.md)。角色 `_aXX` deform/corrective 骨的平行骨链、SOP 四元数后处理和骨架 Mod 约束见 [`SOP骨骼后处理与Deform骨.md`](SOP骨骼后处理与Deform骨.md)。

下一代 C++/ImGui 编辑器的技术选型、构建契约和最小预览目标见 [`CPP编辑器目标与架构.md`](CPP编辑器目标与架构.md)。

## 准备

运行时工具统一放在 `_lib/`，这些第三方 EXE 不提交到仓库：

```text
_lib/flatc.exe
_lib/GraniteTextureReader.exe
_lib/texconv.exe
_lib/nier_cli_mgrr_1.3.0/nier_cli_mgrr.exe
```

- `flatc.exe`：从 [FlatBuffers Releases](https://github.com/google/flatbuffers/releases) 下载 Windows 版。
- `GraniteTextureReader.exe`：使用已验证的 [1.1.5](https://github.com/Nenkai/GraniteTextureReader/releases/tag/1.1.5)。
- `texconv.exe`：使用已验证的 [DirectXTex May 2026](https://github.com/microsoft/DirectXTex/releases/tag/may2026)。
- `nier_cli_mgrr.exe`：下载 [v1.3.0_mgrr](https://github.com/ArthurHeitmann/nier_cli/releases/tag/v1.3.0_mgrr)，解压到上述版本目录。

仓库已经包含对应的 `MMat_ModelMaterial.fbs`。

## 1. 探索角色资源

将角色主体 `.minfo` 拖到 `探索角色资源.bat`。例如：

```text
data/model/pl/pl1400/pl1400.minfo
```

工具会扫描主体以及相同数字 ID 的 `fp`、`wp` 资源，并重建：

```text
explore_output/
  manifest.md                 资源报告，也是构建器入口
  workspace.json              路径映射与初始文件哈希
  source/data/                原始资源副本，只作封包模板
  unpack/data/                可编辑的 DDS、JSON、XML 与模型二进制
  build/data/                 最终 Mod 输出
```

探索器会自动完成：

- 按游戏的 `data/...` 路径复制找到的资源。
- 将 `data/texture/{2k,4k}/*.texture` 解码为 DDS。
- 根据 mmat 的 `A4` 哈希，从 `data/granite/{2k,4k}/gts` 提取 `albd/msk1/msk2/nrml` 以及眼球的 `conj/iris/eyeh` 分层贴图，并转换为 DDS。
- 将 `data/model/**/vars/*.mmat` 解码为 `*.mmat.json`。
- 将角色 cloth 基础组、碰撞体、动作覆盖和重置表解码为 `*.bxm.xml`。
- 将身体 `.mot` 复制到 `source/data/pl/<角色>/`，并将同数字 ID 的表情骨骼 `.mot` 复制到 `source/data/fp/<面部>/`，供 C++ 预览器按需解码；动画不会复制到可编辑的 `unpack`，也不会进入 Mod 构建列表。
- 解析 pl/fp/wp 模型同目录的 `.sop`，在 `manifest.md` 中生成逐骨、逐操作的动画约束表，并把包含原始属性的结构化结果写入 `workspace.json/SkeletonConstraints`。报告分别标注“已探明、部分探明、未探明”和“预览器是否实现”，不会把作用推断当成完整公式。
- 将 pl/fp/wp 的 `.minfo`、`.skeleton` 与 LOD0 `.mmesh` 原样复制到 `unpack`，登记为可恢复、可构建的模型文件。
- 记录中间态 SHA-256，用于识别真实修改。

Granite DDS 输出到 `unpack/data/granite/{2k,4k}/`，格式为：

- `albd`：BC7 sRGB，DX10 头。
- `conj`、`iris`、`eyeh`：BC7 sRGB，DX10 头。
- `msk1`、`msk2`：BC7 线性，DX10 头。
- `nrml`：BC5 UNORM 线性，DX10 头。

转换时会生成完整 mip 链。某些 A4 哈希在当前游戏数据中没有对应 GTP，探索器会把它们记入 `workspace.json` 的 `GraniteMissing`，其余贴图继续处理。

### 眼球与瞳孔贴图

眼球材质不是普通的单张 `albd`。以 `fp1400` 的默认材质为例，左右眼各自通过 mmat 的 `A2` 名称和 `A4` 流式哈希引用多层贴图。编辑器预览会按 alpha 合成 `conj + iris + eyeh` 三个颜色层，`msk1` 作为独立的线性遮罩保留：

- `*_conj`：眼白、结膜和眼球底色。
- `*_iris`：虹膜与瞳孔；修改瞳孔形状、大小或眼睛颜色时主要编辑这一层。
- `*_eyeh`：眼球高光和反光形状。
- `*_msk1`：眼球使用的线性遮罩，不是基础色；左右眼可能共享同一张 `msk1`。

制作眼睛贴图时，应保留 `conj/iris/eyeh` 的透明通道，因为分层合成依赖 alpha。颜色层使用 BC7 sRGB，`msk1` 使用 BC7 线性；不要把 `msk1` 当作缺失的 albedo，也不要只用一张普通 albedo 覆盖整只眼睛。DirectX DDS 在编辑器预览时会统一翻转 V 方向，制作内容时不需要额外手工倒置图像。

面部透明不能只看 `A7`。mmat 的 `A7` 实际是 `shader_sub_type`，真正的 alpha 开关位于 `A1` 参数 `0x53F49792`。以 `fp1400` 为例，主面部 subtype 7 与承载牙齿、舌头的未遮罩 subtype 5 都在不透明 pass 中按 `face_albd.A` 裁切并写深度；眉毛、睫毛覆盖层才使用 `face_albd.A * face_msk2.B`，以 `0.2` 阈值去掉 BC7 低灰噪点，并在不写深度、带深度偏移的透明 pass 中绘制。制作眉毛或睫毛时，应同时检查 albedo alpha 与 msk2 蓝通道，并保持 msk2 为线性数据。视口的“透明覆盖”只控制已确认的 `msk2.B` 覆盖层，不会关闭主面部或口腔实体必须执行的 albedo alpha 裁切。

| 面部材质路径 | mmat 判定 | Coverage | 深度与光栅状态 | 用途 |
| --- | --- | --- | --- | --- |
| 主面部 clip | `A1/0x53F49792 != 0`，非透明覆盖层 | `albd.A` | 不透明 pass，写深度，背面剔除 | 裁掉面部 atlas 中不属于皮肤表面的区域 |
| 口腔实体 clip | alpha 开启、subtype 5，但不属于 `msk2.B` 覆盖槽 | `albd.A` | 不透明 pass，写深度，背面剔除 | 牙齿、舌头等必须按真实几何深度互相遮挡的口腔内容 |
| 遮罩覆盖 | alpha 开启、subtype 5，常量缓冲指向面部覆盖槽 | `albd.A * msk2.B` | alpha blend，不写深度，双面并带深度偏移 | 眉毛、睫毛等贴在脸表面的共面覆盖 |

当前渲染顺序固定为主面部与口腔实体 clip、遮罩覆盖，最后才绘制骨架和碰撞调试线。这里把“裁切”和“混合”分开处理：主面部即使关闭“透明覆盖”也必须继续执行 alpha clip；否则 albedo atlas 的深色无效区域会在眉毛两端显示为黑色三角。口腔实体也必须写深度；把它当成普通透明层会让牙齿、舌头和口腔壁按提交顺序互相覆盖。遮罩覆盖则不能写深度，否则眉毛等共面层会与脸部发生闪烁或随视角消失。

如果要删除眼球材质条目的 `A4`、改用 `data/texture` 下的普通 `.texture`，需要同时构建该材质 `A2` 实际引用的 `conj/iris/eyeh/msk1` 文件；只封回 `iris` 而把其余层留在 Granite 中，不能保证清除 A4 后仍能得到完整眼球效果。默认配色只处理 `vars/0.mmat.json`；其他配色仍需在对应编号的 mmat 中分别处理。

正式网格统一从 `data/model_streaming/lod*/` 收集。为了让 Blender 导入器工作，用户有时会把 LOD0 `.mmesh` 手动复制到 `.minfo` 同目录；探索器会在报告中标记这种辅助副本，但不会将它复制到 `source`，避免与正式流式网格重复。

玩家角色只会按相同数字 ID 自动关联 `fp` 面部/头部与 `wp` 武器资源。`fn` 和 `np` 都是其他独立 NPC 的资源，不属于该玩家角色，因此探索 `pl` 时不会收录它们的模型、流式网格、材质或贴图。只有明确拖入 `fn*.minfo` 或 `np*.minfo` 时，才会将其作为独立资源探索。

`explore_output` 一次只保存一个角色。探索另一个角色会重建该目录；需要保留时，先复制并重命名整个工作区。

## 2. 编辑

只编辑 `unpack/data/`：

```text
unpack/data/texture/2k/foo_0.dds
unpack/data/texture/4k/foo_0.dds
unpack/data/granite/2k/foo_albd.dds
unpack/data/granite/4k/foo_nrml.dds
unpack/data/model/pl/pl1400/vars/0.mmat.json
unpack/data/model/pl/pl1400/pl1400.minfo
unpack/data/model/pl/pl1400/pl1400.skeleton
unpack/data/model_streaming/lod0/pl1400.mmesh
unpack/data/pl/pl1400/cloth/pl1400_0_0_clp.bxm.xml
unpack/data/pl/pl1400/pl1400_0002_0_seq_edit_cloth.bxm.xml
```

`source/` 是不可编辑的原始模板。删除 `unpack/` 中间文件后，该项会标为“缺少输入”：不能构建，但可用“恢复”从原始模板重新生成。

Blender 插件导出完成后，可把整个 `_Exported_MInfo` 文件夹或其中的 `.minfo/.skeleton/.mmesh` 直接拖入编辑封包工具。工具按文件名匹配当前角色的 pl/fp/wp 目标，确认后覆盖对应 `unpack` 文件；插件生成的 `.json` 不进入 Mod 工作区。`.mmesh` 固定写入 `data/model_streaming/lod0/`，LOD1-3 仍只保留在 `source`，供后续分析或可视化使用。

### 动画预览

C++ 编辑器加载 `pl` 模型后扫描 `source/data/pl/<角色>/*.mot`，加载 `fp` 模型后扫描 `source/data/fp/<面部>/*.mot`。后者是实际驱动眼睛、眉毛、嘴部等面部骨骼的表情切片。视口顶部可以按文件名筛选、选择、播放/暂停、回到开头、循环、调整速度或拖动时间轴；选择“静止姿态”会直接恢复原始顶点和绑定骨架，不再重复执行蒙皮。动画只用于预览，不会修改 `unpack`、`source` 或 `build`。

`*_seq_edit_facialmotion.bxm` 记录身体动作时间轴在何时切换哪一个 `AnimationNo/AnimationType` 表情，它不是表情顶点数据本身。当前编辑器先提供 fp `.mot` 切片的直接选择和播放，还没有把身体动作与该调度表自动合成为一条时间轴。

`.mot` 使用 60 FPS 帧时间。动画记录中的骨骼 ID 不是 `.skeleton` 数组下标，而是十六进制骨骼名编码，例如 `0xC45 -> _c45`；对象级 `-1` 轨道映射到根骨 `_900`。当前解码器支持样本中出现的压缩类型 `0-8`，并将位置、欧拉旋转和缩放轨道应用到本地骨骼姿态。CPU 只计算层级姿态和最多 512 个蒙皮矩阵，顶点位置与法线由 GPU 顶点着色器实时蒙皮，不再为每帧动画重写整份顶点缓冲。解码器独立位于 `include/gbfr/formats/animation.hpp` 与 `src/gbfr_formats/src/animation.cpp`，可以脱离编辑器 UI 复用。

当前预览一次只播放一个骨骼动画切片，不处理游戏状态机、切片混合、身体/表情自动联播、IK 或动画驱动的 cloth 参数。未知辅助属性会被保留在解码结果中，但不会错误地套到骨骼 TRS 上。

身体 skeleton 中 `_aXX` 组不是普通动画骨：`.mot` 不包含它们的轨道，游戏会在动画采样后使用模型同目录的 `.sop` 从上臂、大腿等正常骨派生其 swing/twist 与 corrective 姿态。它们故意挂在驱动骨的上一级形成平行骨链，不能只按 skeleton 父级继承。完整结构和公式见 [`SOP骨骼后处理与Deform骨.md`](SOP骨骼后处理与Deform骨.md)。

### `vars/*.mmat` 编号与配色

`vars/0.mmat` 是角色默认配色使用的完整材质配置。`vars/1.mmat` 到 `vars/10.mmat` 是其他配色的并列完整配置，并不是继承 `0.mmat` 的差异补丁。

不同编号通常保持相同的材质条目、Shader 参数、法线和遮罩，只替换需要变色部件的 albedo 与对应 Granite hash。例如：

```text
vars/0.mmat   -> pl1400_body01_lod0_albd
vars/1.mmat   -> pl1400_body01_lod0_c01_albd
vars/2.mmat   -> pl1400_body01_lod0_c02_albd
...
vars/10.mmat  -> pl1400_body01_lod0_c10_albd
```

并非每个部件在所有配色中都会变化；皮肤、部分刀鞘或某些配色下的头发仍可能引用基础贴图。以 `pl1400` 为例，主体 `pl` 与面部 `fp` 都有 `0–10.mmat`，武器 `wp` 只有 `0.mmat`。

- 只修改默认配色：编辑 `vars/0.mmat.json`。
- 希望所有配色使用相同材质逻辑：同步检查并修改 `vars/0–10.mmat.json`。
- 删除 Granite `A4`：必须在希望生效的每个编号中分别删除；只修改 `0` 不会影响其他配色。
- 多个编号引用同名 nrml/msk 时，共享 DDS 可以只生成一份，但每个编号的 mmat 引用仍独立生效。

Granite DDS 是可编辑中间态，构建器不会修改 GTS/GTP。对于 `source/data/texture` 中没有同名原件的 DDS，探索器会把它登记为“新建贴图”候选；构建器使用 nier_cli 创建新的 WTB `.texture` 到 `build/data/texture/{2k,4k}/`。同时在对应 `.mmat.json` 材质条目中删除 `A4`，材质就会改用 `A2.Name` 指向的普通贴图。

新建贴图默认只在 DDS 内容变化后自动勾选。若 DDS 无需编辑、只是要从 Granite 改为普通贴图，需要在构建器中手动勾选对应“新建贴图”项目。

## 3. 构建 Mod

双击 `GBFR编辑封包工具.bat`，选择 `explore_output/manifest.md`。BAT 会通过无窗口启动器打开编辑器，不会保留额外的命令行控制台。

构建器会：

- 按“工作区构建 / mmat 编辑 / cloth 编辑 / skeleton 编辑”划分功能页；构建列表中的“编辑”会跳到对应的专用编辑页。
- `skeleton 编辑` 以主体 `.skeleton` 的骨骼层级为入口，按当前 CLH 文件或全部 CLH 文件汇总该骨骼上的碰撞参数；修改会写回各自的 `unpack/*.clh.bxm.xml`，再由构建页封回 BXM，`.skeleton` 本身不会被改写。
- 构建列表中点击 CLH 行的“编辑”会直接进入 `skeleton 编辑` 并选中该 CLH；也可以直接打开分页查看全部文件模式。
- 分别列出已有贴图封回、新建贴图、mmat 编码与 cloth BXM 编码操作。
- 列出 `.minfo/.skeleton/LOD0 .mmesh` 模型项，支持拖入 Blender 导出结果、修改检测、逐项恢复及原样构建。
- 默认勾选哈希已变化的中间文件。
- 允许手动勾选未修改项目以强制构建。
- “刷新列表”会重新读取当前 DDS/JSON/XML 状态，并保留手动勾选。
- 每一行的“恢复”按钮会恢复单个 `unpack` 文件；勾选多项后可用底部“恢复选中项”批量恢复。普通 DDS/JSON/XML 来自 `source`，Granite DDS 按原 GTS/hash 重新提取；已删除文件会以“缺少输入”保留在列表中，也可以恢复。
- mmat JSON 行提供“编辑”按钮。mmat 编辑页按 `Entries1` 条目列出 A1/A2/A3/A4 数量、A5 标识和 `A2.Name` 贴图引用；顶部“清除全部 A4”会删除该 JSON 中所有材质条目的 A4、自动标记为已修改并勾选，其他字段保持不变。
- 探索器会把 CLP、CLH、动作覆盖和重置表解码为 `*.bxm.xml` 并登记到工作区。cloth 编辑页按文件类型显示 Header、节点图、碰撞体、动作轨道或重置动作列表；当前视图只读，XML 可在外部编辑后由构建器封回 BXM。
- 在写入前显示完整目标清单。
- 只覆盖明确勾选的目标，不清空 `build` 中其他文件。

恢复操作会覆盖所选 `unpack` 文件，但不会修改 `source`、`build` 或游戏目录。命令行可使用：

```powershell
.\GBFR_WorkspaceBuilder.ps1 -ManifestPath .\explore_output\manifest.md -RestoreChanged
```

## cloth 文件组织

角色 cloth 由基础组、动作覆盖和重置列表三层组成：

```text
data/pl/<角色>/cloth/<角色>_0_<N>_clp.bxm   求解组 N：参数与节点拓扑
data/pl/<角色>/cloth/<角色>_0_<N>_clh.bxm   碰撞层 N：可复用碰撞体集合
data/pl/<角色>/cloth/<角色>_rmslst.bxm      动画切换时的布料重置列表
data/pl/<角色>/<角色>_<动作>_<变体>_seq_edit_cloth.bxm
                                                动画时间轴上的布料覆盖
```

`_clp` 解码后的根节点是 `CLOTH`。`CLOTH_HEADER` 保存重力、空气/风阻、拉伸、局部重力、地面碰撞、移动响应和碰撞 Flags 等整组参数；`CLOTH_WK_LIST` 保存节点图。节点通过 `noUp/noDown/noSide/noPoly/noFix` 相互引用，`4095` 表示无连接。

CLP 与 CLH **不是同编号一对一配对**。CLP 是独立求解组，Header 中的 `useCollisionFlags_` 是 CLH 碰撞层位掩码：bit N 表示该求解组使用 `<角色>_0_N_clh.bxm`。同一 CLH 可被多个 CLP 复用，一个 CLP 也可同时使用多个 CLH。编辑器以 CLP 求解组为入口自动叠加掩码指定的碰撞层；CLH 下拉列表只负责检查和编辑单独的碰撞层。

当前 `pl1400` 的关联结果如下，部件名称来自节点空间、蒙皮材质和 submesh 交叉验证，不是文件内明文：

| CLP 求解组 | 节点对应部件 | `useCollisionFlags_` | 使用的 CLH |
|---:|---|---:|---|
| 0 | 十束后发 | 18 | 1、4 |
| 1 | 两束前发 | 97 | 0、5、6 |
| 2 | 七列裙摆 | 4 | 2 |
| 3 | 短侧发 | 2 | 1 |
| 4 | 长辫 | 1 | 0 |
| 5 | 身体衣物与前部挂件，仍需继续细分 | 8 | 3 |
| 6 | `a40_sheath` 刀鞘长链 | 128 | 7 |
| 7 | 胸前软变形 | 0 | 无碰撞层 |

这也解释了错误预览中“前发只有头部碰撞”的现象：旧逻辑画的是 CLP1 + CLH1，实际 CLP1 应同时使用 CLH0、CLH5、CLH6。CLH7 还引用了 CLP2 的 `_c45.._c65` 动态裙摆节点，并由刀鞘求解组6通过 bit7 使用，形成 `裙摆组2 -> 碰撞层7 -> 刀鞘组6` 的跨组依赖；这提示真实运行时存在求解顺序，不能把所有组完全并行处理。

CLP 连接目前可按拓扑关系理解：`noUp/noDown` 是同一布料链的纵向相邻节点，样本中双向互相引用；`noSide` 是相邻链之间的横向边，只保存一次方向，另一端不必再反向写一条；`noPoly` 是多边形关系。`pl1400` 中 52 条有效 `noSide` 与 `noPoly` 完全相同，说明它们共同描述布料带/布料面的横向结构，但 `noPoly` 在求解器中究竟控制四边形、剪切还是其他约束仍未证实。预览将纵向边显示为绿色、横向边显示为品红；只有 `noPoly != noSide` 时才额外用橙色显示，避免把同一条横边画两遍。横向边是布料结构约束，不是碰撞体，因此“一条横向连接”已经足以表达两个节点的关系。

`_clh` 解码后的根节点是 `CLOTH_AT`。每条 `ClothCollision` 记录描述一个碰撞端点：`p1/p2` 是两根附着骨骼，`offset1/offset2` 是各自骨骼局部空间中的位置，`weight` 在两个变换后的位置之间插值：

```text
attachment1 = TransformPoint(BoneWorld(p1), offset1)
attachment2 = TransformPoint(BoneWorld(p2), offset2)
center      = attachment1 * (1 - weight) + attachment2 * weight
```

因此 `p1/p2` **不是胶囊两端**。端点本身以 `center + radius` 构成球；当 `capsule >= 0` 时，它引用同一 CLH 文件中另一条碰撞记录的真实 `id_`，两个记录的球心与各自半径共同构成胶囊或变半径胶囊。`capsule = -1` 表示独立球。碰撞 ID 可能不连续，动作文件和 capsule 链都必须按真实 `id_` 引用，不能用列表行号替代。编辑器会显示“球”“胶囊 -> ID”或“无效引用”，并使用动画后的完整骨骼世界变换绘制 offset。

这个结论来自现有文件的结构关联，不是官方公开的运行时代码。当前 8 组 `pl1400` 样本共有 112 条 CLH 记录：91 条 `p1 == p2` 且 `weight == 0`，另外 21 条 `p1 != p2` 全部使用非零权重；46 条记录的 `capsule` 指向同组内另一个碰撞 `id_`。例如 `_0_0_clh` 中存在 `id 1 -> 0`、`2 -> 1`、`3 -> 2` 的连续 capsule 链；`id 4 -> 0` 又横向连接另一侧端点，随后 `5 -> 4`、`6 -> 5`、`7 -> 6`。这与沿肢体串联胶囊、再用单个引用建立横向胶囊完全吻合，也说明横向胶囊不需要反向再存一条。后续若从游戏运行时或更多角色样本得到反例，应以新证据修正规则。

CLH 参数的当前解释如下：`radius` 是该端点的球半径；`offset1/offset2` 是骨骼局部偏移，必须随骨骼旋转和缩放；`weight` 是双附着插值权重，不是质量或碰撞强度；`capsule` 是另一端点 ID，不是布尔形状开关；`notUseInBattle/notUseInIdle` 分别控制战斗/待机状态是否禁用。两个状态字段的切换时机仍需游戏内验证。CLP 节点的 `rotLimit` 很可能是弧度制关节摆角限制，`friction`、`thick_`、`windForceArea_` 分别对应摩擦、节点碰撞厚度和受风面积；`weight_` 更像求解权重而非直接的公斤质量。`gravityBlendRate_`、`originalRate_`、`jointScale_`、`axisAdjustRate_` 和允许缩放标志的精确公式尚未探明，编辑器不应擅自归一化或改写。

CLP 的 `no/noUp/noDown/noSide/noPoly/noFix` 与 CLH 的 `p1/p2` 使用的是**骨骼名编码**，不是 `.skeleton` 的骨骼数组下标，也不是 FlatBuffers 中的 `BoneId`。数值按十六进制转换后就是 Blender 插件导入的原始骨骼名：例如 `3141 = 0xC45` 对应 `_c45`，`545 = 0x221` 对应 `_221`。编辑器使用 `unpack/data/model/pl/<角色>/<角色>.skeleton` 验证并显示名称，因此拖入 Blender 导出的新骨架后会立即刷新；它不会混用可能出现重名的 fp/wp 骨架，未命中的引用会明确显示“未在主体骨架中找到”。

当前 `pl1400` 样本中，CLP 的 175 个不同骨骼引用与 CLH 的 38 个不同端点引用都能在 `pl1400.skeleton` 中找到。骨骼名 `_xxx` 是游戏原始标识；Blender 插件中的“Translate Bones”只会把一部分通用人体骨骼另行翻译为 `Hips/Chest/Head` 等便于阅读的别名，cloth 专用骨骼通常仍保留 `_c45` 这类名字。

编辑器内优先使用 Blender 插件的首选 humanoid 名称，并在括号内保留游戏原名，例如 `Hips (_000)`、`Chest (_003)`；没有映射的骨骼仍显示 `_c45`。插件原表中重复书写的 `_221`、`_121` 按连续手指编号意图补为 `_221/_222/_223`、`_121/_122/_123`。这只改变显示与下拉选项，保存时仍写入原始骨骼编码。

在 `skeleton 编辑` 页中，左侧骨骼列表显示骨骼 ID、原始名称、父骨骼、碰撞数和来源文件数；右侧按来源 CLH 与碰撞 `id_` 显示该骨骼作为 `p1`、`p2` 或两者时的全部记录。`p1/p2` 使用主体骨架名称下拉选择，`weight/radius/offset1/offset2/capsule/notUseInBattle/notUseInIdle` 可直接编辑。碰撞 `id_` 和来源文件只读，因为动作覆盖和 capsule 链可能引用这些 ID；需要整体恢复时仍使用构建页的逐项“恢复”。

动作覆盖文件的 `Seq/ClothTrack` 记录使用 `FileId` 指向 0–7 编号空间；当记录携带 `CollisionId0..N` 时，ID 均能在 `FileId` 对应的 CLH 碰撞层中找到。记录在 `StartTime` 应用 `ScaleRate`、`FadeInFrame`、`FloorAdditiveOffset` 等覆盖。`StartTime` 基本位于 60 FPS 帧网格上。实际数据中 `ScaleRate` 可出现 `0`、`2.6`、`97`、`100`，编辑器不能把它限制在 `0..1`。

`pl1400` 的 43 个 `seq_edit_cloth` 全部能一一匹配同名 MOT，共含 229 条覆盖记录；单个动作最多同时操作 5 个 FileId。它们更像动作期间对常驻求解组和碰撞层做定时覆盖，而不是临时拼装基础文件。对全部 524 个 MOT 的扫描还发现：0–5 与 7 组普遍存在 cloth 骨骼轨道，主要是旋转目标，其中裙摆组2的帧间变化最密集；刀鞘组6仅在一个 MOT 中出现常量轨道，基本依赖物理解算。当前更合理的执行模型是 MOT 先给出 cloth 骨骼目标姿态，再由 CLP 参数、拓扑和选中的 CLH 层进行二次修正。`originalRate_` 很可能参与回到动画原姿态的混合，但精确公式仍需运行时验证。

`SeqFlag=0` 的记录主要携带碰撞与 Scale 覆盖；当前样本中 `SeqFlag=1/3` 没有碰撞引用，主要改变地面附加偏移。两个 Flag 的精确运行时位含义仍需游戏内验证。`RESET_MOT_LIST` 则列出需要重置布料状态的动作切换。

BXM 是大端序二进制 XML：16 字节头后依次为扁平节点表、键值偏移表和去重字符串池。官方 [GBFRDataTools](https://github.com/Nenkai/GBFRDataTools) 可在 BXM/XML 间语义往返；重建后的基础文件可能因去重策略不同而变大，但再次解码的 XML 内容一致。

输出路径保持游戏结构：

```text
build/
  data/
    texture/2k/*.texture
    texture/4k/*.texture
    model/<类型>/<角色>/vars/*.mmat
    pl/<角色>/cloth/*.bxm
    pl/<角色>/*_seq_edit_cloth.bxm
```

`build/` 可以直接作为 Mod 根目录使用。

## 目录职责

```text
GBFR_modtools/
  探索角色资源.bat             用户入口：建立工作区
  explore_char.ps1            角色资源扫描与中间态生成
  GBFR编辑封包工具.bat         用户入口：选择性构建
  GBFR_WorkspaceBuilder.ps1   构建器 GUI 与变更检测
  README.md

  _lib/
    launch_workspace_builder.vbs 编辑器无控制台启动器
    flatc.exe                 mmat FlatBuffers 编解码
    GraniteTextureReader.exe Granite GTS/GTP 图层提取
    texconv.exe               TGA 到指定 BC 格式 DDS 转换
    GBFRDataTools.exe         BXM/XML 编解码
    nier_cli_mgrr_1.3.0/      从 DDS 新建 WTB texture
    MMat_ModelMaterial.fbs    mmat FlatBuffers schema
    workspace_lib.ps1         WTB 与 mmat 共享读写逻辑
    sop_report.ps1            SOP 约束报告二进制解析器
    sop_operations_zh.json    SOP 操作名称、作用与探明/实现状态目录
    explore_strings_zh.json   探索器中文文案
    builder_strings_zh.json   构建器中文文案
    humanoid_bone_names.json Blender 插件 humanoid 骨骼首选名称映射

  explore_output/             自动生成，已被 Git 忽略
```

## 当前范围

- 自动解码和封回 `data/texture` 下的 WTB `.texture`。
- 自动解码和编码角色 `vars/*.mmat`。
- 自动解码和封回角色 cloth 基础组、碰撞体、动作覆盖与重置表 BXM。
- 自动从 Granite GTS/GTP 提取可用的 `albd/msk1/msk2/nrml/conj/iris/eyeh`，并转换为对应格式的 DDS。
- 为没有普通贴图原件的 Granite DDS 选择性新建 WTB `.texture`，不修改 GTS/GTP。
- WTB 构建以 `source` 原件为模板，按槽位替换 DDS，并保留未编辑槽位。
