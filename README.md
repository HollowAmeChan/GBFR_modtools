# GBFR_modtools

蔚蓝幻想：Relink 纹理模组制作工具包，拖拽即用的 GUI 工具。

## 配置

将以下文件放入本文件夹根目录（与 `GBFR提取工具.bat` 同级）：

| 文件 | 下载地址 |
|------|----------|
| `GraniteTextureReader.exe` | https://github.com/Nenkai/GraniteTextureReader/releases |
| `flatc.exe` | https://github.com/google/flatbuffers/releases → `Windows.flatc.binary.zip` 解压 |

以上文件均已加入 `.gitignore`，随时替换新版本即可，脚本使用相对路径无需修改。

`MMat_ModelMaterial.fbs`（FlatBuffers schema）已包含在仓库中。

## 使用方法

双击 `GBFR提取工具.bat` 打开 GUI。

---

### Extract 标签页 — 提取贴图

**左侧列表**：拖入 `.minfo` 文件。一个完整角色由多个模型部件组成，建议全部加入：

| 目录前缀 | 部件 |
|----------|------|
| `pl/pl1400` | 玩家主体（身体、头发、皮肤） |
| `fp/fp1400` | 脸部细节 |
| `fn/fn1400` | 面部表情 |
| `np/np1400` | NPC 版本 |
| `wp/wp1400` | 武器（刀鞘等） |

**右侧列表**：拖入 `.gts` 文件（可同时拖入多个，逐个搜索）。

常用 `.gts` 路径：
```
data/granite/2k/gts/1/1.gts   ← 角色主体 / 脸部
data/granite/2k/gts/0/0.gts
data/granite/4k/gts/1/1.gts   ← 4K 高分辨率版
```

点击 **Extract** 开始提取。

输出：`output_textures/<角色ID>/<gts名称>/`

---

### Pack 标签页 — 打包贴图 & 编辑材质

#### WTB 贴图（data/texture/2k/*.texture）

这类贴图（主要是 msk3/msk4/msk5 遮罩通道）不经过 Granite 流式系统，以 WTB 格式独立存储。

1. **左侧列表**：拖入游戏原始 `.texture` 文件（来自 `data/texture/2k/`）
2. 点击 **Extract DDS** → DDS 文件保存至 `output_dds/`
3. 在 Photoshop / Substance 等软件中编辑 DDS
4. **右侧列表**：拖入编辑好的 `.dds` 文件
5. 点击 **Pack to .texture** → 输出至 `output_mod/data/texture/2k/`

命名规则：`pl1400_skin_lod0_msk2_0.dds` → `pl1400_skin_lod0_msk2.texture`

#### mmat 材质配置编辑（需要 `flatc.exe`）

mmat 文件定义了材质的着色器参数、贴图引用等配置，使用 FlatBuffers 二进制格式。

1. **左侧列表**：拖入 `.mmat` 文件，点击 **Decode to JSON**
   - JSON 文件输出后自动打开所在目录
2. 用任意文本编辑器编辑 JSON（`A2` 字段下的 `Name` 为贴图引用名）
3. **右侧列表**：拖入编辑好的 `.json` 文件，点击 **Encode to .mmat**
   - 输出：`output_mod/mmat_repacked/<n>.mmat`

---

## 贴图类型与来源

| 类型后缀 | 来源 | 工具 |
|----------|------|------|
| `_albd`、`_nrml`、`_msk1`、`_msk2` | Granite GTS 流式贴图系统 | GraniteTextureReader |
| `_msk3`、`_msk4`、`_msk5` | `data/texture/2k/*.texture`（WTB 格式） | 内置 WTB 打包器 |

---

## 原理说明（提取部分）

`mmat` 是 FlatBuffers 二进制文件，但其中的字符串字段以 ASCII 明文存储。
使用正则表达式即可直接提取其中的 64 位十六进制哈希值，**无需 flatc**。

这些 64 位哈希同时也是：
- `.gtp` 瓦片文件的文件名后缀（`N_<hash>.gtp`）
- `GraniteTextureReader extract -f <hash>` 的参数值

每个哈希对应一个完整的材质变体（包含所有贴图层：漫反射、法线、遮罩、自发光），
使用 `-l -1` 参数一次性提取全部层。

---

## 输出的 Mod 文件结构

```
output_mod/
  data/
    texture/
      2k/
        pl1400_skin_lod0_msk2.texture   ← 重新打包的 WTB
        ...
  mmat_repacked/
    0.mmat                              ← 编辑后的材质配置
    ...
```

将 `output_mod/data/` 目录放入 Reloaded-II 的 mod 文件夹中即可注入。

---

## 文件结构

```
GBFR_modtools/
  GBFR提取工具.bat              ← 主入口，双击打开 GUI
  GBFR_Extractor.ps1           ← GUI 全部逻辑
  MMat_ModelMaterial.fbs       ← mmat 的 FlatBuffers schema（已内置）
  _step1_parse_mmat.ps1        ← 命令行备用：minfo → 哈希表
  _step2_extract_tex.ps1       ← 命令行备用：哈希表 → 提取贴图
  GraniteTextureReader.exe     ← 自行下载放入（已 gitignore）
  flatc.exe                    ← 自行下载放入（已 gitignore）
  nier_cli_mgrr_<版本>/        ← 可选，已 gitignore
  output_hashes/               ← 自动创建，已 gitignore
  output_textures/             ← 自动创建，已 gitignore
  output_dds/                  ← 自动创建，已 gitignore
  output_mod/                  ← 自动创建，已 gitignore
```
