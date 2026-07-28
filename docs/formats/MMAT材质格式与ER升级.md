# MMAT 材质格式与 Endless Ragnarok 升级

本文记录 `.mmat` 的当前解码边界、编辑器检查能力和 Endless Ragnarok（ER）材质升级注意事项。字段定义以 [GBFRDataTools 2.0.0 的 FlatBuffers schema](https://github.com/Nenkai/GBFRDataTools/blob/2.0.0/GBFRDataTools.FlatBuffers/MMat_ModelMaterial.fbs) 为准；字段用途参考 [Relink Modding Wiki 的 MMAT 格式页](https://nenkai.github.io/relink-modding/resources/formats/mmat/) 与 [ER 模型升级说明](https://nenkai.github.io/relink-modding/models/updating_models_for_er/)。

## 已确认根因

工作区中的 `source/*.mmat` 由 GBFRDataTools 2.0.0 完整解包，二进制本身没有缺字段。此前本仓库的 `_lib/MMat_ModelMaterial.fbs` 仍是旧占位 schema，将正式字段显示为 `Entries1/Entries2/A1/A2/A3/A4`，并存在以下错误类型映射：

- 根级 `shader_param_float_data_pool:[float]` 被当成字符串。
- `constant_buffer_indices:[ushort]` 被当成 `[int]`，多个索引会被合并成错误整数。
- `ConstantBuffer.buffer:[uint]` 被当成 `[float]`，原始 32 位位模式可能在 JSON 序列化时丢失。
- `granite_params` 只显示为旧名 `A4`，其他渲染状态只显示为无意义编号。

因此，旧 JSON 不是新版 JSON 的等价别名，不能作为通用编辑格式继续使用。奶刀工作区的 `source` 与当前游戏原件 SHA-256 一致；进一步逐字段比较现有 6 个 build 中的 62 个材质后，渲染状态、参数实际值、贴图引用和被引用的 constant buffer 均与 source 相同，唯一差异是用户主动移除了全部 Granite 配置。用当前 schema 重封相同 unpack JSON 后又与现有 build 62/62 完全一致，证明当前编码器没有再丢字段。不能仅凭文件变小或 Granite 缺失就把发黑归因于封回损坏。旧 schema 的确定问题是无法正确表达带浮点池的材质、字段意义不可见，而且任何新字段编辑都不可靠。

## 必须执行的迁移

升级编辑器后，对现有工作区中的每个 `*.mmat.json` 执行一次“从 source 重新解码 JSON”。新 JSON 的根字段应为：

```text
magic
materials
constant_buffers
shader_param_float_data_pool
unk2 / bool3 / bool4 / bool5
```

如果文件仍以 `Magic/Entries1/Entries2` 开头，编辑器会把它标为“旧 A1/A2/A4 JSON”：允许只读检查，但 UI 和底层构建器都会拒绝封回。即使部分不含浮点池的旧样本曾经可以碰巧往返，也不要手工把键名改成新名称；字段类型和嵌套关系并不相同。

重新解码只覆盖 `unpack` 中的 JSON；`source` 保持只读，`build` 也不会被自动删除。完成后重新构建对应 mmat，替换旧 build 输出。

## 独立材质检查器

在资源列表选中 mmat，或在快捷操作中点击“打开独立 mmat 检查器”，中央 `Viewport` 会切换到材质视图：

- “渲染状态”：shader type/subtype、shadow type、`ignore_alpha`、`g_TwoSided`、Alpha 参数和未知布尔值。
- “Shader 参数”：哈希/名称、U8/U16/浮点向量类型、值或浮点池偏移、已解码值、用途与置信度。
- “贴图与 Granite”：每条引用最前显示工作区中已解包 DDS 的缩略图，并列出 shader map、贴图名、意义、A/C/D 证据、实际命中的 `unpack` 相对路径、page file、layer 对应关系和 tile set。含 `granite_params` 的材质优先匹配 Granite DDS，普通材质优先匹配 `data/texture`；右键缩略图或该行文字可打开命中文件所在的 `unpack` 文件夹。该页支持按工作区贴图名修改或填充引用、添加常用 Shader map 槽以及精确删除一行。当前仅剩的两个无 RDEF 槽会显示 C 级推断名 `g_OutlineTexture` 与 `g_Mask5`，但封回时仍保留原始哈希。
- “常量缓冲”：首 buffer 在 shader type、基准 Shader 和字节数三者精确匹配时，按 RDEF `ParamBuffer` 字段显示颜色、roughness、光照、变体、植被、冰、水面等值；当前目录覆盖 27 个 shader type，并覆盖完整样本中全部 27,491 条带首缓冲 material。字段名标为 A 级 DXBC RDEF，MMAT type 到具体 DXBC 的绑定单独标为 C 级全量契约。其余 489 条 material 本身没有首缓冲；所有原始 buffer 仍保留每个 32 位 word 的十六进制、uint 和 float 重解释。
- “文件信息”：magic、根级标志和数据规模。

当前阶段提供无损检查、安全封回、已知字段编辑与贴图引用编辑。未知常量缓冲不提供猜测性编辑；后续编辑器必须在写入前保留未修改字段、校验参数排序和索引范围，并提供 source 差异视图。

### 面部描边遮罩

面部材质的 MMAT 槽是 `g_5A2C820C`（界面按 C 级证据显示 `g_OutlineTexture`）。Face 描边顶点 Shader 中对应资源的 RDEF 名为 `g_AnimeMask`：它使用 UV0 采样 DDS 红通道，并把采样值直接乘进描边外扩距离。因此红通道黑色 `0` 会取消该区域描边，白色 `1` 保留描边，灰色提供连续衰减；眼眶和嘴周应在红通道画黑。Alpha 不参与这条已确认的外扩路径。

现有材质若已经有 `g_5A2C820C` 行，只需把该行引用改为自定义贴图名，不要再添加重复槽。若该行缺失，可在“添加贴图引用”中选择“描边遮罩”。把 DDS 放到 `unpack/data/texture/2k` 或 `4k` 后刷新工作区，再分别构建 mmat 和该“手动 DDS”资源；最终 Mod 中需要同时存在修改后的 `.mmat` 和生成的 `.texture`。

Shader 参数页会按 A/B/C/D 显示证据来源：A 为 DXBC RDEF 或正式 schema 命名，B 为游戏 EXE 的运行时行为，C 为完整游戏样本推断，D 为少量样本相关。已经恢复的 RDEF 名称优先于旧 schema 中的 `g_XXXXXXXX` 显示，但 B/C/D 级解释不会改写原哈希。完整统计、反汇编位置和当前未知参数结论见 [MMAT 未知参数与 Shader 研究](MMAT未知参数研究.md)。

## 透明、双面与发黑

社区已经确认 ER 更新了 shader 使用的 constant buffer 和 shader parameter；基于旧版本原件制作、没有同步这些数据的 Mod 可能整体偏暗。自动覆盖原版 constant buffer 只是兜底方案，因为它会破坏 Mod 有意调整过的数值。当前工具选择完整保留 2.0.0 数据，并让用户逐项比较，不自动用原版覆盖自定义材质。对于奶刀当前样本，仍需在检查器中按材质槽比较 source 与实际投放的 mmat，不能把“旧 schema”直接当成发黑的唯一根因。

已知透明组合至少涉及：

- `0x53F49792`：编辑器显示为 `Cutout 丢弃模式`。它使角色材质选择 `_5discard` 管线，使用 Albedo Alpha 丢弃透明像素，中间 Alpha 使用抖动裁切。它不是半透明开关：以 Metal `5/7` 为例，关闭时普通 Shader 忽略 Albedo Alpha，开启时才读取 Alpha 做 discard，两种变体对保留像素都输出 Alpha=1。`ignore_alpha` 控制是否忽略 Alpha，`0x53F49792` 控制是否进入 Cutout 丢弃管线，两者职责不同。FlatBuffers schema 中仍保留上游名称 `g_53F49792_EnableAlpha_GUESSED` 以保持兼容。
- `g_IsUseAlbedoAlphaClip` 与 `g_EnableDiscardMask`：与裁切/丢弃路径有关。
- `shadow_type=2`：schema 命名为 `ShadowEnable_AlphaBlend`。
- `ignore_alpha`：是否忽略 Alpha。
- `g_TwoSided`：A 级双面参数，但当前完整 `pl/fp/wp` 角色样本中一次都没有；它不能作为角色背面问题的默认解释。

这些字段不是一个“透明”开关的五种别名。社区资料明确指出双面加透明的完整设置仍未探明，因此游戏内背面出现不规则半透明时，应先与同模型、同材质槽的原版 ER mmat 比较全部参数和 constant buffer，再检查网格绕序、TAA 与遮罩。编辑器会显示可疑组合，但目前不会自动改写。

角色专项扫描进一步确认：非 Eye 的 `0x53F49792` 与 `ignore_alpha=false` 在当前 4,779 条 `pl/fp/wp` 样本中完全同值；EXE 使用该位选择 `_5discard` 变体，DXBC 随后对 Albedo Alpha 执行抖动 discard。`bool12=true` 与 `shadow_type=3` 在全部 5,501 条角色材质中完全同现；`g_EnableDiscardMask` 全部关闭。这些组合可用于发现 Mod 相对原版的异常设置。

模型视口当前使用独立的简化预览 shader，只采样 Albedo、眼睛贴图和 Alpha mask，并没有消费 MMAT 的 roughness、hatching、rim、forward-light 等 `ParamBuffer` 字段，也没有游戏运行时的 `CutCharacterMaterialDataBuf`。因此视口中的明暗只能用于制作检查，不能作为游戏 MMAT 光照是否正确的验证结果。后续若要逼近游戏材质，必须单独实现角色材质预览路径，不能仅调高现有预览灯光后宣称已复现。

## 验证结果

使用正式 2.0.0 schema 对 `pl1400/vars/0.mmat`、`fp1400/vars/0.mmat` 和新版 `pl2900/vars/0.mmat` 执行“二进制 -> JSON -> 二进制 -> JSON”往返，前后 JSON 语义完全一致。重新编码后的 FlatBuffers 字节布局和源文件不要求逐字节相同；验证标准是再次解码后所有字段、uint 位模式、索引和数组顺序一致。
