# GBFR modtools

GBFR：Relink 模组工具包，拖拽即用。

## 配置

把此文件夹放在任意位置，然后将以下发行版可执行文件放进文件夹根目录（和 `.bat` 文件同级）：

| 文件 | 下载地址 |
|------|----------|
| `GraniteTextureReader.exe` | https://github.com/Nenkai/GraniteTextureReader/releases |

这是唯一的外部依赖。不需要 flatc、不需要 GBFRDataTools、不需要 Python。

更新时直接替换 `.exe` 即可，脚本通过相对路径引用，无需任何修改。

## 使用流程

### 第一步 —— minfo 转哈希表

将 `.minfo` 文件拖拽到 `01_minfo转Hash表.bat` 上运行。

`.minfo` 必须和它的 `vars/` 文件夹在同一目录下（即 GBFRDataTools 解包后的标准结构）：

```
pl1400/
  pl1400.minfo   ← 拖这个
  vars/
    0.mmat
    1.mmat
    ...
```

输出：`output_hashes/<角色名>_hashes.txt`

### 第二步 —— 哈希表提取纹理

将 `.gts` 文件拖拽到 `02_Hash表提取纹理.bat` 上运行。

游戏数据中常见的 `.gts` 文件位置：
```
data/granite/2k/gts/0/0.gts
data/granite/2k/gts/1/1.gts   ← 角色 / 脸部
data/granite/2k/gts/2/2.gts   ← 眼睛
data/granite/4k/gts/...
```

如果某个哈希在当前 `.gts` 中找不到，工具会报告失败。换一个 `.gts` 文件重新拖入即可。

`output_hashes/` 中若有多个角色的哈希文件，工具会列出并提示选择。

输出：`output_textures/<角色名>/`

## 原理简述

`mmat` 是 FlatBuffers 二进制格式，但其中的字符串字段以 ASCII 明文存储，因此可以直接用正则提取 64 位十六进制字符串，无需 flatc 解析。

这些 64 位字符串同时也是：
- `.gtp` 瓦片文件的文件名组成部分（`N_<hash>.gtp`）
- `GraniteTextureReader.exe extract -f <哈希>` 的参数值

每个哈希对应一个完整的材质变体（包含所有纹理层：漫反射、法线、遮罩、自发光等），使用 `-l -1` 参数一次性提取全部层。

## 目录结构

```
modtoolspack/
  01_minfo转Hash表.bat        ← 第一步入口（拖 .minfo 到此）
  02_Hash表提取纹理.bat         ← 第二步入口（拖 .gts 到此）
  _step1_parse_mmat.ps1       ← 第一步逻辑（内部）
  _step2_extract_tex.ps1      ← 第二步逻辑（内部）
  GraniteTextureReader.exe    ← 自行下载放入（见配置）
  output_hashes/              ← 第一步输出，自动创建
  output_textures/            ← 第二步输出，自动创建
```

以 `_` 开头的文件为内部脚本，用户只需关注两个 `.bat` 文件。

`output_hashes/` 和 `output_textures/` 已加入 `.gitignore`，提交仓库时不会包含。
