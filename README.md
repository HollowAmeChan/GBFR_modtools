# GBFR Modtools

用于建立《碧蓝幻想：Relink》角色资源工作区，并把编辑后的 DDS 与 mmat JSON 构建为可直接安装的 Mod 文件。

模型与骨架的 Blender 编辑、导出和安装流程见 [`模型与骨架Mod制作流程.md`](模型与骨架Mod制作流程.md)。

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
  unpack/data/                可编辑的 DDS、*.mmat.json 与 cloth *.bxm.xml
  build/data/                 最终 Mod 输出
```

探索器会自动完成：

- 按游戏的 `data/...` 路径复制找到的资源。
- 将 `data/texture/{2k,4k}/*.texture` 解码为 DDS。
- 根据 mmat 的 `A4` 哈希，从 `data/granite/{2k,4k}/gts` 提取 `albd/msk1/msk2/nrml` 并转换为 DDS。
- 将 `data/model/**/vars/*.mmat` 解码为 `*.mmat.json`。
- 将角色 cloth 基础组、碰撞体、动作覆盖和重置表解码为 `*.bxm.xml`。
- 记录中间态 SHA-256，用于识别真实修改。

Granite DDS 输出到 `unpack/data/granite/{2k,4k}/`，格式为：

- `albd`：BC7 sRGB，DX10 头。
- `msk1`、`msk2`：BC7 线性，DX10 头。
- `nrml`：BC5 UNORM 线性，DX10 头。

转换时会生成完整 mip 链。某些 A4 哈希在当前游戏数据中没有对应 GTP，探索器会把它们记入 `workspace.json` 的 `GraniteMissing`，其余贴图继续处理。

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
unpack/data/pl/pl1400/cloth/pl1400_0_0_clp.bxm.xml
unpack/data/pl/pl1400/pl1400_0002_0_seq_edit_cloth.bxm.xml
```

`source/` 是不可编辑的原始模板。删除 `unpack/` 中间文件后，该项会标为“缺少输入”：不能构建，但可用“恢复”从原始模板重新生成。

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

- 按“工作区构建 / mmat 编辑 / cloth 编辑”划分功能页；构建列表中的“编辑”会跳到对应的专用编辑页。
- 分别列出已有贴图封回、新建贴图、mmat 编码与 cloth BXM 编码操作。
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
data/pl/<角色>/cloth/<角色>_0_<N>_clp.bxm   基础布料组 N：求解参数与节点拓扑
data/pl/<角色>/cloth/<角色>_0_<N>_clh.bxm   基础布料组 N：碰撞体
data/pl/<角色>/cloth/<角色>_rmslst.bxm      动画切换时的布料重置列表
data/pl/<角色>/<角色>_<动作>_<变体>_seq_edit_cloth.bxm
                                                动画时间轴上的布料覆盖
```

`_clp` 解码后的根节点是 `CLOTH`。`CLOTH_HEADER` 保存重力、空气/风阻、拉伸、局部重力、地面碰撞、移动响应和碰撞 Flags 等整组参数；`CLOTH_WK_LIST` 保存节点图。节点通过 `noUp/noDown/noSide/noPoly/noFix` 相互引用，`4095` 表示无连接。部分组存在大量横向和多边形连接，不能一律当作线性骨链。

`_clh` 解码后的根节点是 `CLOTH_AT`，其中每个 `ClothCollision` 有独立的 `id_`、`p1/p2`、偏移、半径、权重和 capsule 链接。碰撞 ID 可能不连续，动作文件必须按真实 `id_` 引用，不能用列表行号替代。

动作覆盖文件的 `Seq/ClothTrack` 记录以 `FileId` 指向同编号基础组，以 `CollisionId0..N` 指向该组 `_clh` 中的碰撞体，并在 `StartTime` 应用 `ScaleRate`、`FadeInFrame`、`FloorAdditiveOffset` 等覆盖。`StartTime` 基本位于 60 FPS 帧网格上。实际数据中 `ScaleRate` 可出现 `0`、`2.6`、`97`、`100`，编辑器不能把它限制在 `0..1`。

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
    explore_strings_zh.json   探索器中文文案
    builder_strings_zh.json   构建器中文文案

  explore_output/             自动生成，已被 Git 忽略
```

## 当前范围

- 自动解码和封回 `data/texture` 下的 WTB `.texture`。
- 自动解码和编码角色 `vars/*.mmat`。
- 自动解码和封回角色 cloth 基础组、碰撞体、动作覆盖与重置表 BXM。
- 自动从 Granite GTS/GTP 提取可用的 `albd/msk1/msk2/nrml`，并转换为对应格式的 DDS。
- 为没有普通贴图原件的 Granite DDS 选择性新建 WTB `.texture`，不修改 GTS/GTP。
- WTB 构建以 `source` 原件为模板，按槽位替换 DDS，并保留未编辑槽位。
