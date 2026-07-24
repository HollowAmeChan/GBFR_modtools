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
2. `gbfr_shader_disasm` 用系统 `D3DDisassemble` 输出带指令编号和字节偏移的 DXBC 汇编，用于追踪实例缓冲、采样和 discard 数据流。
3. `analyze_mmat.py` 用正式 2.0.0 FlatBuffers schema 直接读取全部 MMAT，并用 GBFRDataTools 的定制 XXHash32 关联名称。
4. `summarize_mmat_patterns.py` 生成取值、类别、shader family、稀有样本和参数对列联表。
5. `find_hash_constants.py` 在 PE section 中定位哈希常量，再用 `dumpbin /DISASM /RANGE` 检查实际取参和分支行为。
6. `compare_mmat_records.py` 对两份分析结果按材质哈希比较渲染状态、参数实际值、贴图、被引用的常量缓冲和 Granite 配置，忽略 FlatBuffers 的非语义字节布局差异。
7. `analyze_character_mmat.py` 只抽取 `model/{pl,fp,wp}` 下 Eye、Face、Hair、Metal、Skin 五类角色材质，逐字段解码 `ParamBuffer`，并验证角色渲染状态不变量。
8. `compare_character_mmat.py` 按角色模型与配色对比材质家族、状态、贴图槽、首缓冲字节指纹和 RDEF 字段值集合。
9. `generate_mmat_cbuffer_catalog.py` 从指定 DXBC 基准生成编辑器字段目录，并对全量 MMAT 做严格覆盖校验；任何新的带首缓冲 shader type、非法索引或长度偏差都会使生成失败。

生成的 `research_output/mmat` 约有上百 MB，已被 Git 忽略；仓库提交的是脚本、精炼结论和以后可由编辑器读取的字段目录。

## 角色专项样本

角色研究主范围固定为 `model/pl`、`model/fp` 和 `model/wp`。敌人、特效或场景道具即使复用了 Eye/Face/Hair/Metal/Skin shader，也只用于旁证，不与玩家角色样本混算。当前专项扫描覆盖 268 个角色模型、1,035 个 MMAT 文件和 5,501 个 material entry：Metal 2,499、Face 1,091、Hair 776、Eye 722、Skin 413。共解码 73,867 个 `ParamBuffer` 字段值和 29,507 个 Shader 参数值。

五类角色 `ParamBuffer` 的首缓冲尺寸全部与 DXBC RDEF 精确匹配，错误数为 0。该结果排除了“当前新 DLC 角色换了另一套未知首缓冲布局，而编辑器仍按旧长度截断”的假设，但不排除字段值、贴图内容、渲染状态或游戏运行时全局角色光照不同。

专项样本还给出以下完整集合不变量：

- 非 Eye 的 4,779 条角色材质均含 `0x53F49792`，其布尔值与 `ignore_alpha=false` 精确一致，错误数为 0。它仍只能命名为角色 pass key 位 `0x4`，不能仅凭相关性改名为透明开关。
- 5,501 条材质中 `bool12=true` 与 `shadow_type=3` 精确一致；`bool9` 恒为 false，`bool10` 恒为 true。
- `g_EnableDiscardMask` 在 3,221 条 Eye/Metal 材质中存在，但全部为 false。
- A 级 `g_TwoSided` 在 `pl/fp/wp` 中一次都没有；它只属于当前样本中的 PlantShake/场景资产，不能用于解释角色背面的异常半透。
- `0x8B8038FC` 在 4,779 条非 Eye 角色材质中存在但全部为 false。全量数据中的 21 个真值来自 `em/fe` 等复用角色 shader 的资产，不代表普通 `pl/fp/wp` 会启用 subtype 13。
- Metal 的 `0xA6EB1B34` 在 2,499 条样本中存在，981 个真值与 `shadow_type=3 / bool12=true` 完全同现，1,518 个假值均不属于该状态，错误数为 0。
- Metal 的 `0x9C83F56F` 同样在 2,499 条样本中存在，但只有 18 个真值，全部集中于 `pl1100/pl1101/pl1102/pl1700` 的 `vars/10`。

这些关系由 `character_invariants.json` 自动生成，游戏更新后应重新扫描，而不是把当前计数写成永久格式规则。

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

## 贴图槽名称覆盖

76 种 MMAT texture map hash 中，74 种可由 DXBC `RDEF` 资源名直接恢复。剩余两种没有出现在 Shader RDEF 或游戏 EXE 的原始字符串中，因此不达到 A 级；但候选名称经定制 XXHash32 精确复算，并与完整样本逐项吻合，按 C 级显示：

| 哈希 | UI 推断名 | 全量证据 |
|---|---|---|
| `0x5A2C820C` | `g_OutlineTexture` | 7,534 个槽；角色材质几乎全部引用 `pre_outline`，并固定处于 Eye/Face/Hair/Metal/Skin 对应贴图序列的位置；`XXHash32Custom("g_OutlineTexture")` 精确等于该哈希 |
| `0x8A0507FB` | `g_Mask5` | 1,491 个槽；只存在于 Face `3/5`、`3/7`，固定为第 7/8 个槽，实际资源名全部为 `*_msk5`；`XXHash32Custom("g_Mask5")` 精确等于该哈希 |

分析器在 `texture_maps.csv` 中将两者标为 `hash_preimage_full_samples`，编辑器显示“C：哈希预像 + 全量样本”。FlatBuffers schema 和封回数据仍保留 `g_5A2C820C`、`g_8A0507FB` 对应的原始数值，不把推断名称写回二进制。若以后从新版 Shader、EXE 字符串或开发符号找到原名，可将证据升级为 A 级。

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

### Alpha 与另一条独立管线状态

`0x53F49792` 与 `0x8B8038FC` 都出现在同一批 7,287 个复用角色 shader family 的 material，但列联表证明二者不是别名：

| `0x53F49792` | `0x8B8038FC` | material 数 |
|---:|---:|---:|
| 0 | 0 | 5,972 |
| 1 | 0 | 1,294 |
| 0 | 1 | 14 |
| 1 | 1 | 7 |

EXE 对 `0x53F49792` 有 16 个直接读取点。Eye、Face、Hair、Metal、Skin 等角色构建路径都会先检查其类型和值，再与三项材质/pass 布尔状态做 OR；结果为真时给管线选择 key 增加 `0x4`，随后据 key 选择 shader/resource 组合。它不写入 alpha 数值，也不等同于 `ignore_alpha`、`shadow_type` 或 RDEF 已命名的 `g_IsUseAlbedoAlphaClip`。因此当前 B 级行为是“角色材质 pass key 位 `0x4`”；社区的 `EnableAlpha` 只作为 C 级视觉含义线索保留。

`0x8B8038FC` 在 Face、Hair、Metal、Skin 的两组构建路径中共有 8 个读取点。描述表路径 `0x1446E7C1E..0x1446E7CCA`、`0x1446E9B7E..0x1446E9C30`、`0x1446ED197..0x1446ED25E`、`0x1446EEA98..0x1446EEB44` 都在值类型为 U8 且值非零时选中 `0x146138780` 的 24 字节管线状态记录 13；另一组表块对应记录 29，也就是 13 + 16。最终传给管线注册器的 VS/PS 哈希不随该参数改变，因此它不是另一份 `ps_character*` Shader 文件选择器。

四类材质在基表中的转换如下；每一行都只有记录偏移 20 的一个字节发生变化，其余 23 字节不变：

| family | 默认记录 | 启用记录 | 偏移 20 |
|---|---:|---:|---|
| Face | 9 | 13 | `0x80 -> 0x84` |
| Hair | 10 | 13 | `0x81 -> 0x84` |
| Metal | 7 | 13 | `0x47 -> 0x84` |
| Skin | 9 | 13 | `0x80 -> 0x84` |

高表块执行相同的 `+16` 转换；其中 Metal 从记录 23 的 `0x07` 转到记录 29 的 `0x84`，脚本会单独保留而不是把它误写成低表块的复制。当前 B 级行为应表述为“选择角色管线状态表第 13 路径”。该字节最终对应 blend、depth/stencil 还是 rasterizer 状态，仍需运行时 D3D11 状态或帧捕获确认，不能仅凭记录序号命名视觉效果。

全量 21 个真值分布为 Face 10、Hair 4、Metal 4、Skin 3，来自 9 个 MMAT：`em2000/vars/1`、`em7700/vars/0`、`em8200/vars/0,1,2`、`em8210/vars/0`、`fe2000/vars/1`、`fe2100/vars/1`、`fe8200/vars/2`。21 条全部是 `shadow=1`、`bool9=false`、`bool10=true`、`bool12=false`；`ignore_alpha` 同时存在真假，因此不是透明开关。其中既有默认 `vars/0`，也有 `c01/c02` 贴图变体，不能把它命名为“配色开关”。实际 `pl/fp/wp` 的 4,779 个非 Eye 样本全部为 0。

### Elemental 的三层管线模板选择

`0x2AEDA6AD`、`0x93D9F63A`、`0x2B5C866C` 同时存在于 62 个 Elemental LookDev material。EXE `0x1446F01C1..0x1446F0283` 和 `0x1446F0399..0x1446F049C` 读取它们，并据此复制不同的约 `0x68` 字节静态管线描述模板，而不是把值写进 `ParamBuffer`：

- `0x93D9F63A=1` 的 5 个样本正好是全部 shader `7/11`，其余 57 个 `7/8` 均为 0；当前解释为 Elemental `7/11` 管线模板开关。
- `0x2B5C866C=1` 只有 `em7540/vars/0` 的一个 material；它在主模板之后选择另一项附加描述符，当前解释为 Elemental 稀有附加描述符开关。
- `0x2AEDA6AD` 在完整样本中 62/62 均为 0，但运行时代码仍保留真值分支并会选择第三套主模板；只能解释为休眠的 Elemental 管线模板开关，无法由现有资产命名具体效果。

三项行为达到 B 级，样本关系达到 C 级；官方字段名仍未知。

### 其他成组参数

| 参数组 | 样本范围 | 当前结论 |
|---|---|---|
| `0x8B8038FC` | 7,287 个角色 shader family material | 21 个真值选择 24 字节管线状态表记录 13/29；不更换 VS/PS Shader 哈希，视觉状态名未知 |
| `0x9C83F56F`, `0xA6EB1B34` | 4,133 个 Metal `5/7`、`5/5` material | 前者选择备用运行时自发光乘数；后者为阴影类型 3 追加方向驱动的 Alpha 裁切阈值，行为不同 |
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

### Metal 阴影类型 3 的方向驱动 Alpha 裁切

`0xA6EB1B34` 出现在全部 4,133 个 Metal-family material 中，1,246 个为真。真值以玩家角色为主；奶刀 `pl1400` 中身体/衣物部分槽为真，刀鞘槽为假。

角色专项样本把范围收紧到了 2,499 个 Metal：981 个真值全部是 `shadow_type=3 / bool12=true`，1,518 个假值全部不是该状态，错配为 0。它不再只是“身体上较多”的弱相关。

EXE `0x1446EA5F3` 读取该参数。为真时，代码取得对象向量与 0 号骨骼变换（无骨骼时回退到对象变换），将根/对象平移方向归一化并计算点积，随后写入：

`threshold = 0.6 - 0.4 * dot(normalized(root_or_object_position), object_vector)`

当 `objectVector` 按调用约定为单位方向时，标称阈值范围为 `0.2..1.0`；当前函数没有再次归一化它，因此不能把该范围写成无条件格式约束。该 float 写入 `g_InstanceParam` 的第 4 项；已反汇编的 Metal `5discard` 和 depth-only DXBC 都从 `instanceBase + 4` 读取它，再执行 `sampledAlpha < threshold -> discard`。`instanceBase + 5` 是另一项深度/遮挡开关，不能和这个阈值混为一谈。原始字段名尚未恢复，但 CPU 写入、DXBC 消费和角色全量样本三条证据已把行为提升为 B 级。

这解释了为什么受影响材质的破碎边缘可能随模型相对世界原点的方向变化。它只能裁掉接近阈值的像素，不会直接制造法线或光照噪声：如果毛躁发生在完全不透明区域，仍应继续检查切线、法线、精度和阴影；如果只发生在 alpha 边缘或带灰度遮罩的平面，它就是首要候选。

`0x9C83F56F` 在同一构建函数的 `0x1446EA83A` 被读取。真值且类型正确时，CPU 将 Metal 运行时结构 `+0x0C` 的 32 位 float 写入着色子记录的 `+2`；否则写入默认的 `+0x08`。子记录 `+3` 始终来自结构 `+0x10`。当方向 Alpha 路径存在时，CPU 会在该子记录前插入五项数据，因此这里的 `+2/+3` 是相对着色子记录的槽位，不能写成整个实例缓冲的固定绝对索引。Metal DXBC 只在 `g_EnableEmissive` 为真时读取这两项并计算 `[2] * [3]`，再与自发光贴图、`g_EmissiveIntensity` 和角色材质全局强度共同生成自发光贡献。因此旧结论“备用管线 / 资源描述符”已被否定，当前 B 级行为名为“选择备用运行时自发光第一乘数”。

全量 4,133 个 Metal 中只有 22 个真值：角色范围内 18 个全部来自 `pl1100/pl1101/pl1102/pl1700` 的 `vars/10`，贴图均为 `_c10_` 特殊盔甲/布料变体；另 4 个来自 `em0010/em7530/em7531/em7600`。解码后的场景配置还公开了 `emissiveIntensity_`、`itemEmissiveIntensity_` 与拼写如此的 `useNewEmissiveParamters_`，说明运行时确有多套自发光强度，但目前没有结构偏移或原始名称字符串把 `0x9C83F56F` 唯一绑定到其中某一项。`Item Emissive` 只能作为后续 C 级候选，不能写成正式字段名。

## Constant Buffer 的 RDEF 解码

MMAT 的 `constant_buffer_indices` 首项可与对应 shader family 的 `ParamBuffer` 直接对齐。`pl1400` 的 Metal 首 buffer 为 96 bytes，Hair/Skin 为 48 bytes，均与 DXBC RDEF 精确匹配。Eye、Face、Hair、Metal、Skin 的所有像素 shader 变体在各自 family 内也保持同一布局。

当前版本的 `pl1400/vars/0` 与新 DLC `pl2900/vars/0` 对照结果为：Hair 与 Skin 的首缓冲字节完全相同；Metal 的字段集合、Shader 参数哈希集合和贴图槽哈希集合相同，唯一 RDEF 字段值差异是 `g_EnableRimLight=true` 对 `false`。`pl1400` 另有 3 个 Metal 条目采用 `shadow_type=3/bool12=true`，`pl2900` 没有。这只能证明两个当前角色资产继续使用同一契约，不能证明 ER 升级前后同一资产的值没有变化；版本升级结论仍需要历史版本的同一 MMAT 作为基线。

检查器只在“shader type + 基准 Shader + 首 buffer 字节数”精确匹配时显示字段表，可直接查看：

- Eye：瞳孔缩放、parallax bias、颜色变体、虹膜自发光遮罩。
- Face：皱纹/脸颊颜色、roughness、mouth/tooth、joint position、flat normal、forward light。
- Hair：anisotropic width、roughness、emissive、face color blend、forward light。
- Metal：hatching、rim light、specular color、emissive、颜色/roughness 变体和各类 mask 开关。
- Skin：roughness、hatching、颜色变体、outline、forward light。
- Elemental、Flowmap、Glowing、Ice、Lava、Lucilius：自发光、Fresnel、视差、闪烁、容器、晶体和流动参数。
- UberEnv 及其 2/3/4 层、两种 pivot painter、textureless：逐层 UV、颜色/金属/粗糙度覆盖、法线、湿润、植被风、grass shine、贴图平铺和 camera fade。
- Plant middle/shake、Sky cloud、Water lake、Grid：对应 RDEF 中的植被、天空、水面和网格参数。

目录由 `generate_mmat_cbuffer_catalog.py` 从 27 个明确指定的 DXBC 基准文件生成，生成时校验每个 `ParamBuffer` 的预期字节数、字段对齐和字段类型。它覆盖完整样本中全部 27,491 条带首缓冲的 material；其余 489 条本身没有首缓冲。字段名来自 A 级 RDEF，MMAT shader type 到基准 DXBC 的绑定仍单独标为 C 级全量契约。未匹配的 buffer 继续只显示 raw/hex/float，不按相似长度强行套布局。

本次补齐了此前仅有的两类带缓冲缺口：

- shader type 1 共 43 条，全部是 `1/6` 且首缓冲为 48 字节。基准 `ps/model/foward/ps_characterconstant.pso` 的 RDEF 给出 `g_AlbedoColor`、`g_Intensity`、`g_UvAnimation`、`g_bAlbedoOverWrite`、`g_UseNormalMap`、`g_UberEmissivePower`；布尔槽和仅出现的 `600/1300` emissive 值均与样本字节契约一致。
- shader type 17 共 49 条，全部是 `17/1` 且首缓冲为 16 字节。基准 `vs/model/vs_plantbillboard.vso` 的 RDEF 给出 `g_GeneralWindStrength`、`g_SwingNoiseSpeed`、`g_IsUseYAxisBillboard`；前三个 4 字节槽的值域与全量样本一致，末尾 4 字节恒为零填充。

以上字段名本身是 A 级 RDEF 证据；“MMAT type 1/17 使用该 DXBC 布局”是由 shader family、全量长度和值域共同支持的 C 级绑定，编辑器会把两种等级分开显示。

## 奶刀工作区的封回语义验证

对奶刀工作区当前已有的 6 个 `build/*.mmat`（`fp1400 vars/0` 与 `pl1400 vars/0,7,8,9,10`）做了逐材质比较，共匹配 62 个 material：

- shader type/subtype、shadow 和四个布尔渲染状态：62/62 相同。
- Shader 参数的类型和实际值：62/62 相同。
- 贴图引用及顺序：62/62 相同。
- 每个材质实际引用的 constant buffer 哈希和全部 uint word：62/62 相同。
- 唯一差异是 source 的 62 个 `granite_params` 在 build 中均为空。

继续检查 `unpack/*.mmat.json` 后确认，这些 JSON 本身已经不含 `granite_params`；用当前 schema 和 `flatc` 临时重封全部 6 个 JSON，再与现有 build 比较，62/62 个材质完全无差异。因此 Granite 的移除发生在用户显式执行“清除 Granite 流式引用”时，不是当前 MMAT 编码器暗中丢字段。将 Granite 贴图改成独立 `.texture` 时这可能是有意的资源路径切换，不能单独证明它是发黑根因。

## 复现命令

先构建 `gbfr_shader_reflect` 和 `gbfr_shader_disasm`，然后在仓库根目录执行：

```powershell
$gbfrPython = "D:\Blender\Blender 4.5\4.5\python\bin\python.exe"

out\bin\RelWithDebInfo\gbfr_shader_reflect.exe `
  "D:\Steam\steamapps\common\Granblue Fantasy Relink\data\shader" `
  research_output\mmat\shader_reflection.jsonl

out\bin\RelWithDebInfo\gbfr_shader_disasm.exe `
  "D:\Steam\steamapps\common\Granblue Fantasy Relink\data\shader\ps\model\foward\lookdev\ps_charactermetallookdev_5discard.pso" `
  research_output\mmat\ps_charactermetallookdev_5discard.asm

$dumpbin = "D:\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64\dumpbin.exe"
$gameExe = "D:\Steam\steamapps\common\Granblue Fantasy Relink\granblue_fantasy_relink.exe"
& $dumpbin /DISASM /RANGE:0x1446EA5F0,0x1446EA77A $gameExe
& $dumpbin /RAWDATA /RANGE:0x1454A8EF0,0x1454A8EF4 $gameExe
& $dumpbin /RAWDATA /RANGE:0x1454A4860,0x1454A4864 $gameExe

# 0x8B8038FC：Face / Hair / Metal / Skin 的状态表选择与 24 字节记录表
& $dumpbin /DISASM /RANGE:0x1446E7C1E,0x1446E7CCA $gameExe
& $dumpbin /DISASM /RANGE:0x1446E9B7E,0x1446E9C30 $gameExe
& $dumpbin /DISASM /RANGE:0x1446ED197,0x1446ED25E $gameExe
& $dumpbin /DISASM /RANGE:0x1446EEA98,0x1446EEB44 $gameExe
& $dumpbin /RAWDATA /RANGE:0x146138780,0x146138A80 $gameExe

& $gbfrPython scripts\research\find_pe_relative_xrefs.py `
  $gameExe 0x1444B7540 0x146138780 --rip-relative `
  --output "research_output\mmat\pe_xrefs.csv"

& $gbfrPython scripts\research\analyze_mmat.py `
  --data-root "D:\Steam\steamapps\common\Granblue Fantasy Relink\data" `
  --flatc "_lib\tools\flatc.exe" `
  --schema "_lib\MMat_ModelMaterial.fbs" `
  --flatbuffers-runtime "_references\GBFRBlenderTools-v2.0.0\Entities" `
  --shader-jsonl "research_output\mmat\shader_reflection.jsonl" `
  --string-binary "D:\Steam\steamapps\common\Granblue Fantasy Relink\granblue_fantasy_relink.exe" `
  --out-dir "research_output\mmat"

& $gbfrPython scripts\research\generate_mmat_cbuffer_catalog.py `
  --reflection "research_output\mmat\shader_reflection.jsonl" `
  --materials "research_output\mmat\materials.jsonl" `
  --coverage-output "research_output\mmat\parambuffer_coverage.json" `
  --output "src\gbfr_editor\src\mmat_param_layouts.generated.hpp"

& $gbfrPython scripts\research\analyze_character_mmat.py `
  --materials "research_output\mmat\materials.jsonl" `
  --reflection "research_output\mmat\shader_reflection.jsonl" `
  --parameter-catalog "research_output\mmat\shader_parameters.csv" `
  --out-dir "research_output\mmat\characters"

& $gbfrPython scripts\research\analyze_subtype13_pipeline.py `
  --materials "research_output\mmat\materials.jsonl" `
  --binary "D:\Steam\steamapps\common\Granblue Fantasy Relink\granblue_fantasy_relink.exe" `
  --output "research_output\mmat\subtype13_pipeline.json"

& $gbfrPython scripts\research\compare_character_mmat.py `
  --materials "research_output\mmat\characters\character_materials.csv" `
  --fields "research_output\mmat\characters\character_parambuffer_fields.csv" `
  --left-model pl1400 `
  --right-model pl2900 `
  --output "research_output\mmat\characters\pl1400_vs_pl2900.json"

& $gbfrPython scripts\research\compare_mmat_records.py `
  --baseline "research_output\milk_knife_source\materials.jsonl" `
  --candidate "research_output\milk_knife_build\materials.jsonl" `
  --output "research_output\milk_knife_build_compare.json"
```

模式汇总脚本的 `--hash` 可以重复传入。它会输出 `parameter_patterns.json` 和所有选中参数对的 `parameter_pair_values.csv`。

## 下一步验证

1. 追踪 face 世界空间中心最终绑定到哪个 constant buffer 和 shader 变量，确认它是光照中心、阴影中心还是视线中心。
2. 对 `0x8B8038FC` 的状态表记录 13 路径做运行时 A/B 或帧捕获，确认记录偏移 20 的 `0x84` 最终对应 blend、depth/stencil 还是 rasterizer 状态及其视觉语义。
3. 用运行时 A/B 或帧捕获确认已定位的 pipeline 位最终对应的视觉名称，重点区分透明、深度、阴影和 pass 变体。
4. 找到 ER 升级前的同一角色 MMAT 基线，再与当前版本做逐字段比较；`pl1400` 与 `pl2900` 的横向比较不能替代版本比较。
5. 持续把新证据同步到独立 MMAT 检查器；当前已显示 A/B/C/D 等级和恢复名称，封回仍保留原哈希和值类型。
