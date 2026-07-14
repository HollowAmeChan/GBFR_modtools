# GBFR Modtools

用于建立《碧蓝幻想：Relink》角色资源工作区，并把编辑后的 DDS 与 mmat JSON 构建为可直接安装的 Mod 文件。

模型与骨架的 Blender 编辑、导出和安装流程见 [`模型与骨架Mod制作流程.md`](模型与骨架Mod制作流程.md)。

## 准备

下载 FlatBuffers 的 Windows 版 `flatc.exe`，放到：

```text
_lib/flatc.exe
```

仓库已经包含对应的 `MMat_ModelMaterial.fbs`。当前流程不依赖 GraniteTextureReader 或 nier_cli。

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
  unpack/data/                可编辑的 DDS 与 *.mmat.json
  build/data/                 最终 Mod 输出
```

探索器会自动完成：

- 按游戏的 `data/...` 路径复制找到的资源。
- 将 `data/texture/{2k,4k}/*.texture` 解码为 DDS。
- 将 `data/model/**/vars/*.mmat` 解码为 `*.mmat.json`。
- 记录中间态 SHA-256，用于识别真实修改。

正式网格统一从 `data/model_streaming/lod*/` 收集。为了让 Blender 导入器工作，用户有时会把 LOD0 `.mmesh` 手动复制到 `.minfo` 同目录；探索器会在报告中标记这种辅助副本，但不会将它复制到 `source`，避免与正式流式网格重复。

玩家角色只会按相同数字 ID 自动关联 `fp` 面部/头部与 `wp` 武器资源。`fn` 和 `np` 都是其他独立 NPC 的资源，不属于该玩家角色，因此探索 `pl` 时不会收录它们的模型、流式网格、材质或贴图。只有明确拖入 `fn*.minfo` 或 `np*.minfo` 时，才会将其作为独立资源探索。

`explore_output` 一次只保存一个角色。探索另一个角色会重建该目录；需要保留时，先复制并重命名整个工作区。

## 2. 编辑

只编辑 `unpack/data/`：

```text
unpack/data/texture/2k/foo_0.dds
unpack/data/texture/4k/foo_0.dds
unpack/data/model/pl/pl1400/vars/0.mmat.json
```

`source/` 是不可编辑的原始模板。删除 `unpack/` 中不需要的中间文件，会让它退出构建候选清单。

要把 Granite 流式贴图改为普通 `.texture` 时，可在对应 `.mmat.json` 材质条目中删除 `A4`，同时保证 `A2.Name` 对应的普通贴图会构建到 `data/texture/{2k,4k}/`。

## 3. 构建 Mod

双击 `GBFR编辑封包工具.bat`，选择 `explore_output/manifest.md`。

构建器会：

- 列出贴图封包与 mmat 编码操作。
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
    flatc.exe                 第三方运行时，自行下载
    MMat_ModelMaterial.fbs    mmat FlatBuffers schema
    workspace_lib.ps1         WTB 与 mmat 共享读写逻辑
    explore_strings_zh.json   探索器中文文案
    builder_strings_zh.json   构建器中文文案

  explore_output/             自动生成，已被 Git 忽略
```

## 当前范围

- 自动解码和封回 `data/texture` 下的 WTB `.texture`。
- 自动解码和编码角色 `vars/*.mmat`。
- Granite GTS/GTP 流式贴图目前只在 manifest 中记录引用，不自动解码或封包。
- WTB 构建以 `source` 原件为模板，按槽位替换 DDS，并保留未编辑槽位。
