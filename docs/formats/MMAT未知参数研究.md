# MMAT 未知参数与 Shader 研究

本文记录对完整游戏数据中 `.mmat`、DXBC Shader 和游戏运行时代码的交叉分析。目标是区分“字段名已恢复”“运行时行为已确认”和“仅由样本相关性推断”，避免把哈希共现误写成官方名称。完整本机输入位置见 [本机资源路径](../本机资源路径.md)。

## 证据等级

| 等级 | 判断标准 | UI 可采用的表述 |
|---|---|---|
| A | DXBC `RDEF` 反射直接给出名称，且自定义 XXHash32 与 MMAT 哈希一致 | 字段命名已确认 |
| B | EXE 明确按该哈希取参，并能从分支、资源选择或变换流程确认行为 | 运行时行为已确认；名称仍可未知 |
| C | 大样本类别、shader family、贴图命名与取值关系高度一致 | 高置信度推断 |
| D | 只有共现或少量样本 | 待验证 |

除非找到原始名称字符串或开发符号，B/C 级参数在 schema 中继续保留 `g_XXXXXXXX`，中文解释必须带“推断”或“行为”。

## 全量样本与方法

当前扫描覆盖游戏 `data` 下全部 7,712 个 MMAT，共 27,980 个 material entry，解析失败 0 个。样本包含 36 种 shader parameter hash、76 种 texture map hash 和 2,983 种 constant-buffer hash。

Shader 目录实际存放裸 DXBC：1,162 个 `.pso`、265 个 `.vso`、170 个 `.cso`、1 个 `.hso`、1 个 `.dso`。`gbfr_shader_reflect` 已成功反射 1,599 个文件，失败 0 个，得到 2,116 个可哈希的变量、资源和 constant-buffer 名称。36 个 MMAT 参数中，15 个可由 Shader `RDEF` 直接恢复名称，1 个只有 schema 名称，20 个仍是哈希名。

研究链路：

1. `gbfr_shader_reflect` 提取每个 DXBC 的 RDEF 变量、资源和布局。
2. `analyze_mmat.py` 用正式 2.0.0 FlatBuffers schema 直接读取全部 MMAT，并用 GBFRDataTools 的定制 XXHash32 关联名称。
3. `summarize_mmat_patterns.py` 生成取值、类别、shader family、稀有样本和参数对列联表。
4. `find_hash_constants.py` 在 PE section 中定位哈希常量，再用 `dumpbin /DISASM /RANGE` 检查实际取参和分支行为。
5. `compare_mmat_records.py` 对两份分析结果按材质哈希比较渲染状态、参数实际值、贴图、被引用的常量缓冲和 Granite 配置，忽略 FlatBuffers 的非语义字节布局差异。

生成的 `research_output/mmat` 约有上百 MB，已被 Git 忽略；仓库提交的是脚本、精炼结论和以后可由编辑器读取的字段目录。

## 已恢复的 Shader 名称

下列名称达到 A 级证据，均来自 DXBC 反射并通过定制 XXHash32 复算：

| 哈希 | 名称 | 备注 |
|---|---|---|
| `0x06CFE5A4` | `g_EmissivePower` | 自发光强度 |
| `0x24C1ABA9` | `g_EnableDiscardMask` | discard mask 开关 |
| `0x372C03F0` | `g_tsubasa_Param0_4stGimmick` | 关卡/对象特化参数，不应泛化命名 |
| `0x3C966EE3` | `g_UseIceEmissive0` | 冰材质的一组自发光开关 |
| `0x49D8C1B9` | `g_EnableOutLine` | 描边开关 |
| `0x60F31A22` | `g_IsUseAlbedoAlphaClip` | Albedo alpha clip |
| `0x6C5CB9AC` | `g_IsUseDetailNormal` | 细节法线 |
| `0x7920C84F` | `g_IsUseDitherMap` | 抖动贴图 |
| `0x8E6B4C53` | `g_UseCubeMapReflection` | 当前 2.0.0 schema 枚举未列出，但 MMAT 中实际存在 |
| `0x920821E1` | `g_EnableBooleanMask` | 布尔遮罩 |
| `0x98EBBEC2` | `g_SwayAmplitude` | 摆动幅度 |
| `0x9F1DA064` | `g_ContainerUse` | container 路径开关 |
| `0xCA06A6B6` | `g_UseIceEmissive` | 冰材质自发光 |
| `0xD94F2821` | `g_TwoSided` | 双面材质 |
| `0xE208C4C4` | `g_UseColorNoise` | 颜色噪声 |

`0xB460A0F0` 的 `g_IsUseDepthFade` 当前来自正式 schema，而不是本批 Shader 反射结果，因此单独记为 schema 命名证据。

## 已收紧的未知参数

### 左眼与右眼材质索引

`0xBAEF6920` 和 `0xE56343C0` 都出现在 1,567 个 eye/skin-family material 中，但不是重复开关：

| `0xBAEF6920` | `0xE56343C0` | material 数 |
|---:|---:|---:|
| 0 | 0 | 586 |
| 1 | 0 | 491 |
| 0 | 1 | 490 |

`0xBAEF6920=1` 的命中样本对应 `_r_eye_`，`0xE56343C0=1` 对应 `_l_eye_`。EXE `0x140974F3E..0x140974F86` 逐 material 读取两者，并分别写入两个不同索引数组；前者为真时写第二组，后者为真时写第一组。行为达到 B 级，贴图样本达到 C 级。当前解释为：

- `0xBAEF6920`：右眼材质标记（高置信度推断）。
- `0xE56343C0`：左眼材质标记（高置信度推断）。

### 面部中心骨骼与局部偏移

`0x11664BFC` 与 `0x56346692` 同时只出现在 shader `3/5`、`3/7` 的 1,491 个 face material 中。前者是布尔值；后者是 Vec3，常见值为 `(0, 1.3, -7)`，另有 `(0, 0, 0)`、`(0, -1.564, -2.6)` 等。

EXE `0x1446E66E3..0x1446E6935` 的行为是：当 `0x11664BFC` 为真时取得索引 5 的骨骼变换，把 `0x56346692` 作为局部三维偏移乘入该变换，最终写出一个世界空间位置。它们不是透明或双面开关。结合 face family，当前 B/C 级解释为：

- `0x11664BFC`：启用“由头部骨骼计算面部参考中心”的路径。
- `0x56346692`：该参考中心相对骨骼的局部偏移。

具体中心用于面部阴影、光照还是视线计算仍需追踪结果缓冲在 shader 中的消费者，暂不写死原始字段名。

### 冰/晶体专用运行时路径

`0x92339519` 只存在于 247 个 shader type 12 material，173 个为真。真值集中在 `ice`、`crystal`、`ruby` 等资产；假值集中在复用同一 shader type 的雪地、墙体、矿石和保存点等环境资产。

EXE `0x1446F2C15` 与 `0x1446F2CFA` 两次读取该参数。为真时会复制额外资源句柄、建立专用数据并替换其中一组 texture binding；为假时跳过。这达到 B/C 级，可描述为“启用冰/晶体模型专用资源路径”，但尚未恢复官方字段名，也不能直接等同于已有的 `g_UseIceEmissive`。

### Alpha 与另一条独立变体

`0x53F49792` 与 `0x8B8038FC` 都出现在同一批 7,287 个角色类 material，但列联表证明二者不是别名：

| `0x53F49792` | `0x8B8038FC` | material 数 |
|---:|---:|---:|
| 0 | 0 | 5,972 |
| 1 | 0 | 1,294 |
| 0 | 1 | 14 |
| 1 | 1 | 7 |

EXE 在 `0x1446E7B5E` 和 `0x1446E7C1E` 分别取参。`0x53F49792` 影响 alpha/pass 变体选择，但“EnableAlpha”仍是社区命名。`0x8B8038FC` 与另一项布尔状态共同组成索引，在 `0x1446E7C9E..0x1446E7CCA` 从固定表复制一整项管线描述符；它是独立的管线表维度，不是 `0x53F49792` 的别名。其 21 个真值集中在 `vars/1`、`vars/2` 的 `c01/c02` 角色变体，因此当前 B/C 级解释为“选择角色材质的独立管线描述表分支”，原始名称和该分支最终对应的视觉效果仍未确认。

### Elemental 的三层管线模板选择

`0x2AEDA6AD`、`0x93D9F63A`、`0x2B5C866C` 同时存在于 62 个 Elemental LookDev material。EXE `0x1446F01C1..0x1446F0283` 和 `0x1446F0399..0x1446F049C` 读取它们，并据此复制不同的约 `0x68` 字节静态管线描述模板，而不是把值写进 `ParamBuffer`：

- `0x93D9F63A=1` 的 5 个样本正好是全部 shader `7/11`，其余 57 个 `7/8` 均为 0；当前解释为 Elemental `7/11` 管线模板开关。
- `0x2B5C866C=1` 只有 `em7540/vars/0` 的一个 material；它在主模板之后选择另一项附加描述符，当前解释为 Elemental 稀有附加描述符开关。
- `0x2AEDA6AD` 在完整样本中 62/62 均为 0，但运行时代码仍保留真值分支并会选择第三套主模板；只能解释为休眠的 Elemental 管线模板开关，无法由现有资产命名具体效果。

三项行为达到 B 级，样本关系达到 C 级；官方字段名仍未知。

### 其他成组参数

| 参数组 | 样本范围 | 当前结论 |
|---|---|---|
| `0x9C83F56F`, `0xA6EB1B34` | 4,133 个 Metal `5/7`、`5/5` material | 前者选择备用管线/资源描述符，后者启用角色根位置运行时数据；行为不同 |
| `0x037BE4E5` | UberEnv / 植被 | 禁用背面剔除，选择双面光栅路径；官方字段名未知 |
| `0x0A05A26F` | 18 个 foliage `9/1` material | 17 真 1 假；EXE 无直接哈希常量引用，行为仍未知 |
| `0x4298F7E4` | 25 个 sky/cloud `20/1` material | Sky/Cloud 管线 permutation 位 `0x2`，官方视觉名称未知 |

### 能量护盾控制的遗迹材质组索引

`0xAC6F995D` 是唯一使用 U16 的未命名参数，12,784 个样本中 12,712 个为 0；非零值为 1..5。虽然它广泛存在于 UberEnv material，但 EXE `0x1435C9B75` 所在运行时组件直接引用 `energy_shl001/002_green` 资源名，说明消费者是关卡中的能量护盾控制逻辑，不是通用 Shader 层数。

代码逐 material 读取该值：1..4 分别测试一个四位状态掩码中的对应位，并取同索引的强度；5 检查四位是否全部有效并走汇总分支；0 写入禁用值。样本结构与行为一致：`ba4066` 的遗迹中心材质按五个一组依次标为 1、2、3、4、5，第二套材质再重复；`bg46f5` 也用 1 标分段槽、5 标汇总槽。当前 B/C 级解释为“能量护盾控制的遗迹材质组索引”：0 不参与，1..4 为分组，5 为汇总。它不应被显示成泛化的 UberEnv layer count，官方字段名仍未知。

### UberEnv 管线 permutation 位

EXE `0x1446FA6FE..0x1446FA7B7` 在构造 UberEnv 管线 key 时分别读取三个未知布尔参数：

- `0xAB261CFA=1` 给 key 增加 `0x10` 位。
- `0xC5BD3DED=1` 给 key 增加 `0x100` 位。
- `0xC9762248=1` 给 key 增加 `0x200` 位；部分 pass 会直接跳过该位。

`0xC5BD3DED` 只出现在 3,280 个 layer2/layer3 material，202 个为真；另两项的分布也受 UberEnv family 和 pass 限制。它们达到 B 级“管线 permutation 行为”证据，但仍不能仅凭 bit 位置命名成透明、法线、颜色噪声或层数。与 `g_IsUseDepthFade`、`g_UseColorNoise` 等已有 A 级名称不同，检查器只显示 key 位和原始哈希，不改 schema 名称。

### UberEnv / 植被双面光栅路径

`0x037BE4E5` 出现在 14,582 个 UberEnv、UberEnvTextureless、PlantMiddleView 和 PivotPainter material 中，829 个为真。EXE 有 9 个直接读取点；不同 shader 构建器都会用它切换同一对 24 字节静态光栅描述记录，并把对应布尔状态写入各自的 pipeline key。两条记录只有首枚举不同：默认路径为 `2`，真值路径为 `0`，其余 23 字节完全一致。

样本也与该状态吻合：真值高度集中于树叶、藤蔓、绳索、布料、旗帜、帐篷和其他薄片几何，PivotPainter `26/1` 中 307 个材质有 259 个为真。综合运行时状态切换和资产分布，当前达到 B/C 级“禁用背面剔除、选择双面光栅路径”。它与 PlantShake `19/1` 中 A 级命名的 `g_TwoSided` 行为同类，但属于另一组 shader family，未找到原始名称字符串，因此 schema 仍保留 `g_037BE4E5`，不把它强行改名为 `g_TwoSided`。

### Foliage 实例变换首分量

`0xEB6F1AE7` 只出现在 18 个 foliage `9/1` 控制材质，3 个为真。EXE `0x1446F143A..0x1446F1471` 将它转换成 `0.0/1.0`，写入一项 48 字节实例记录的第一个 float，随后从对象复制另外 32 字节变换数据。Shader RDEF 中 `vs_foliage.vso` 的 `g_InstanceWorldTbl` 元素正是 48 字节 `float4x3`，尺寸和绑定用途吻合。因此当前 B 级解释是“控制 foliage 实例世界变换首分量/首轴是否启用”，不是风强度或颜色参数；它造成的具体几何行为仍需运行时 A/B 捕获。

与它共现的 `0x0A05A26F` 有 17 真 1 假，但 EXE 中没有直接出现该哈希常量，可能由表驱动代码读取，也可能是当前版本未消费的兼容字段。没有数据流证据前继续保留 D 级。

### Sky / Cloud 管线 permutation 位

`0x4298F7E4` 只存在于 25 个 sky/cloud `20/1` material，23 真 2 假。EXE `0x1446F8AEE` 和 `0x1446F93AA` 的两条构建路径都会把布尔值乘 2 后并入 pipeline key，再与 pass 类型、材质状态和 subtype 位组合；它不进入 Sky `ParamBuffer`。因此该参数达到 B 级“Sky/Cloud permutation 位 `0x2`”行为证据，但真值不等同于“显示云”“启用透明”或其他具体视觉名称。

### Metal 的角色位置相关运行时数据

`0xA6EB1B34` 出现在全部 4,133 个 Metal-family material 中，1,246 个为真。真值以玩家角色为主；奶刀 `pl1400` 中身体/衣物部分槽为真，刀鞘槽为假。

EXE `0x1446EA5F3` 读取该参数。为真时会取得对象数据与 0 号骨骼变换，归一化根位置相关向量、计算点积并建立一组额外运行时数据；初始化路径 `0x1446EAEB5` 还会进入一段额外资源绑定流程。结合 Metal pixel shader 中只在完整 forward 变体出现的 `CharacterDirectionalDataBuf` 和 `CharacterPointLightDataBuf`，它很可能控制角色专用方向/光照数据，但“光照”仍是推断，原始字段名尚未恢复。当前按 B/C 级显示为“启用角色根位置相关的运行时方向数据”。

这条路径与已观察到的“表面噪波随模型到世界原点方向变化”在现象上相关，但尚未证明因果。必须比较同一模型中该参数真/假的相邻材质，或用帧捕获确认其最终绑定与 shader 消费者后，才能把它列为噪波根因。

`0x9C83F56F` 在同一构建函数的 `0x1446EA83A` 被读取。真值且类型正确时，代码从当前 Metal 描述记录的 `+0x0C` 位置取 32 位 ID；否则取默认的 `+0x08`，随后还会固定写入 `+0x10` 的另一项 ID。它因此是备用管线/资源描述符选择开关，不是颜色、粗糙度或透明度的直接数值。22 个真值多为特殊颜色变体，只能说明资产使用场景，不能作为官方命名。

## Constant Buffer 的 RDEF 解码

MMAT 的 `constant_buffer_indices` 首项可与对应 shader family 的 `ParamBuffer` 直接对齐。`pl1400` 的 Metal 首 buffer 为 96 bytes，Hair/Skin 为 48 bytes，均与 DXBC RDEF 精确匹配。Eye、Face、Hair、Metal、Skin 的所有像素 shader 变体在各自 family 内也保持同一布局。

检查器只在“shader type + 基准 Shader + 首 buffer 字节数”精确匹配时显示字段表，可直接查看：

- Eye：瞳孔缩放、parallax bias、颜色变体、虹膜自发光遮罩。
- Face：皱纹/脸颊颜色、roughness、mouth/tooth、joint position、flat normal、forward light。
- Hair：anisotropic width、roughness、emissive、face color blend、forward light。
- Metal：hatching、rim light、specular color、emissive、颜色/roughness 变体和各类 mask 开关。
- Skin：roughness、hatching、颜色变体、outline、forward light。
- Elemental、Flowmap、Glowing、Ice、Lava、Lucilius：自发光、Fresnel、视差、闪烁、容器、晶体和流动参数。
- UberEnv 及其 2/3/4 层、两种 pivot painter、textureless：逐层 UV、颜色/金属/粗糙度覆盖、法线、湿润、植被风、grass shine、贴图平铺和 camera fade。
- Plant middle/shake、Sky cloud、Water lake、Grid：对应 RDEF 中的植被、天空、水面和网格参数。

目录由 `generate_mmat_cbuffer_catalog.py` 从 25 个明确指定的 DXBC 基准文件生成，生成时校验每个 `ParamBuffer` 的预期字节数、字段对齐和字段类型。它覆盖完整样本中的 27,399/27,980 个 material entry（约 97.9%）。不匹配的 buffer 继续只显示 raw/hex/float，不按相似长度强行套布局；silhouette、foliage、plant billboard 和未知类型等没有可直接对应的 pixel `ParamBuffer`。

## 奶刀工作区的封回语义验证

对奶刀工作区当前已有的 6 个 `build/*.mmat`（`fp1400 vars/0` 与 `pl1400 vars/0,7,8,9,10`）做了逐材质比较，共匹配 62 个 material：

- shader type/subtype、shadow 和四个布尔渲染状态：62/62 相同。
- Shader 参数的类型和实际值：62/62 相同。
- 贴图引用及顺序：62/62 相同。
- 每个材质实际引用的 constant buffer 哈希和全部 uint word：62/62 相同。
- 唯一差异是 source 的 62 个 `granite_params` 在 build 中均为空。

继续检查 `unpack/*.mmat.json` 后确认，这些 JSON 本身已经不含 `granite_params`；用当前 schema 和 `flatc` 临时重封全部 6 个 JSON，再与现有 build 比较，62/62 个材质完全无差异。因此 Granite 的移除发生在用户显式执行“清除 Granite 流式引用”时，不是当前 MMAT 编码器暗中丢字段。将 Granite 贴图改成独立 `.texture` 时这可能是有意的资源路径切换，不能单独证明它是发黑根因。

## 复现命令

先构建 `gbfr_shader_reflect`，然后在仓库根目录执行：

```powershell
out\bin\RelWithDebInfo\gbfr_shader_reflect.exe `
  "D:\Steam\steamapps\common\Granblue Fantasy Relink\data\shader" `
  research_output\mmat\shader_reflection.jsonl

py -3 scripts\research\analyze_mmat.py `
  --data-root "D:\Steam\steamapps\common\Granblue Fantasy Relink\data" `
  --flatc "_lib\tools\flatc.exe" `
  --schema "_lib\MMat_ModelMaterial.fbs" `
  --flatbuffers-runtime "_references\GBFRBlenderTools-v2.0.0\Entities" `
  --shader-jsonl "research_output\mmat\shader_reflection.jsonl" `
  --string-binary "D:\Steam\steamapps\common\Granblue Fantasy Relink\granblue_fantasy_relink.exe" `
  --out-dir "research_output\mmat"

py -3 scripts\research\generate_mmat_cbuffer_catalog.py `
  --reflection "research_output\mmat\shader_reflection.jsonl" `
  --materials "research_output\mmat\materials.jsonl" `
  --output "src\gbfr_editor\src\mmat_param_layouts.generated.hpp"

py -3 scripts\research\compare_mmat_records.py `
  --baseline "research_output\milk_knife_source\materials.jsonl" `
  --candidate "research_output\milk_knife_build\materials.jsonl" `
  --output "research_output\milk_knife_build_compare.json"
```

模式汇总脚本的 `--hash` 可以重复传入。它会输出 `parameter_patterns.json` 和所有选中参数对的 `parameter_pair_values.csv`。

## 下一步验证

1. 追踪 face 世界空间中心最终绑定到哪个 constant buffer 和 shader 变量，确认它是光照中心、阴影中心还是视线中心。
2. 对 `0x8B8038FC` 的 21 个真值资产逐一与同角色 `vars/0` 对比 constant buffer、texture map 和实际 pipeline 表项。
3. 用运行时 A/B 或帧捕获确认已定位的 pipeline 位最终对应的视觉名称，重点区分透明、深度、阴影和 pass 变体。
4. 对新 DLC 与旧版同类材质做逐字段 ParamBuffer 对比，继续定位“材质整体发黑”的版本差异。
5. 持续把新证据同步到独立 MMAT 检查器；当前已显示 A/B/C/D 等级和恢复名称，封回仍保留原哈希和值类型。
