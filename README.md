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

工具会扫描主体以及相同数字 ID 的 `fn`、`fp`、`np`、`wp` 资源，并重建：

```text
explore_output/
  manifest.md                 资源报告，也是构建器入口
  workspace.json              路径映射与初始文件哈希
  source/data/                原始资源副本，只作封包模板
  unpack/data/                可编辑的普通贴图、Granite DDS 与 *.mmat.json
  build/data/                 最终 Mod 输出
```

探索器会自动完成：

- 按游戏的 `data/...` 路径复制找到的资源。
- 将 `data/texture/{2k,4k}/*.texture` 解码为 DDS。
- 根据 mmat 的 `A4` 哈希，从 `data/granite/{2k,4k}/gts` 提取 `albd/msk1/msk2/nrml` 并转换为 DDS。
- 将 `data/model/**/vars/*.mmat` 解码为 `*.mmat.json`。
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
```

`source/` 是不可编辑的原始模板。删除 `unpack/` 中不需要的中间文件，会让它退出构建候选清单。

Granite DDS 是可编辑中间态，构建器不会修改 GTS/GTP。对于 `source/data/texture` 中没有同名原件的 DDS，探索器会把它登记为“新建贴图”候选；构建器使用 nier_cli 创建新的 WTB `.texture` 到 `build/data/texture/{2k,4k}/`。同时在对应 `.mmat.json` 材质条目中删除 `A4`，材质就会改用 `A2.Name` 指向的普通贴图。

新建贴图默认只在 DDS 内容变化后自动勾选。若 DDS 无需编辑、只是要从 Granite 改为普通贴图，需要在构建器中手动勾选对应“新建贴图”项目。

## 3. 构建 Mod

双击 `GBFR编辑封包工具.bat`，选择 `explore_output/manifest.md`。

构建器会：

- 分别列出已有贴图封回、新建贴图与 mmat 编码操作。
- 默认勾选哈希已变化的中间文件。
- 允许手动勾选未修改项目以强制构建。
- 在写入前显示完整目标清单。
- 只覆盖明确勾选的目标，不清空 `build` 中其他文件。

输出路径保持游戏结构：

```text
build/
  data/
    texture/2k/*.texture
    texture/4k/*.texture
    model/<类型>/<角色>/vars/*.mmat
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
    flatc.exe                 mmat FlatBuffers 编解码
    GraniteTextureReader.exe Granite GTS/GTP 图层提取
    texconv.exe               TGA 到指定 BC 格式 DDS 转换
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
- 自动从 Granite GTS/GTP 提取可用的 `albd/msk1/msk2/nrml`，并转换为对应格式的 DDS。
- 为没有普通贴图原件的 Granite DDS 选择性新建 WTB `.texture`，不修改 GTS/GTP。
- WTB 构建以 `source` 原件为模板，按槽位替换 DDS，并保留未编辑槽位。
