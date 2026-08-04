# SOP 骨骼后处理与 Deform 骨

本文记录 GBFR 角色骨架中 `_aXX` deform/corrective 骨的运行方式。它们不是普通动画骨，也不是 Blender 文件内的约束；游戏在采样 `.mot` 后，使用同模型目录下的 `.sop` 逐帧生成这些骨骼的局部姿态。

## 1. 四类资源的职责

| 资源 | 职责 |
|---|---|
| `.skeleton` | 骨骼名称、父级和静止局部 TRS |
| `.mot` | 正常动画骨的局部位移、欧拉旋转和缩放轨道 |
| `.sop` | 从正常动画骨派生 deform/corrective 骨姿态的角色级操作表 |
| `*_seq_edit_ik.bxm` | 动作时间线上的脚部 IK、踝高和角度限制开关 |

`seq_edit_ik` 不负责大臂、大腿的 deform。Blender 插件现在会随主体读取、保留和显式导出 `.sop`，并为已确认的基础 Swing/Twist 操作建立局部 Copy Rotation 近似；其余操作会显示但不伪装成可执行约束。

## 2. 平行骨链

Deform 骨经常挂在驱动骨的父级，而不是驱动骨本身。这是有意设计的平行骨链：

```text
正常手臂                         Deform 分支
Shoulder _00a                   Shoulder _00a
  `- UpperArm _00b               `- UpperArmDeform _a0b <- SOP(_00b)
       `- LowerArm _00c         UpperArm _00b
            `- Hand _00d          `- LowerArmDeform _a0c <- SOP(_00c)
                                LowerArm _00c
                                  `- HandDeform _a0d <- SOP(_00d)
```

腿部同理：

```text
Hips _000
  |- UpperLeg _012
  `- UpperLegDeform _a12 <- SOP(_012)
```

这种父级关系让 SOP 可以只写入经过缩放的 swing/twist，避免 deform 骨先完整继承驱动旋转后再重复变换。只按 `.skeleton` 父级播放 `.mot` 时，deform 骨会停留在静止局部姿态，和真正旋转的上臂或大腿分离，混合蒙皮因此产生拉伸。

## 3. SOP 二进制结构

当前所有玩家角色 `.sop` 都使用同一基础结构：

```text
0x00  char[4]  "sop\0"
0x04  uint32   版本，当前为 0x20200309
0x08  uint32   操作数量
0x0C  uint32[] 每条操作的文件绝对偏移
```

操作记录是可变长的 hashed-property 对象：

- 第一个 `uint32` 是操作类型哈希。
- `0x5B0292DD` 后的值是 target bone 名称编码。
- `0x1B5B0525` 后的值是 source bone 名称编码。
- 骨骼值是 `_xxx` 名称中的十六进制数，不是 skeleton 数组下标。
- 其余属性以“字段哈希、值类型、值”保存；当前样本的值类型 `0` 为整数、`1` 为浮点。
- 同一 target 可以有多条操作，必须保留文件顺序并累积执行。

`pl1400.sop` 有 101 条操作、84 个不同 target 和 16 个 source。84 个 target 恰好覆盖 skeleton 中全部 `_aXX` 组骨骼。35 个玩家角色样本都使用版本 `20200309`，操作类型来自同一组 7 个哈希。

## 4. 已验证的核心操作

以下名称是按数学行为命名，不代表游戏内部的正式类名。

### Swing-twist 分配 (`0xB1FFF4E6`)

先把 source 局部四元数按指定骨轴分解：

```text
Qsource = Qswing * Qtwist
Qtarget = pow(Qswing, swingRate) * pow(Qtwist, twistRate) * Qoffset
```

`pow(q, rate)` 表示把单位四元数到 `q` 的旋转角乘以 `rate`。在 `pl1400` 中：

| Target | Source | 骨轴 | swingRate | twistRate |
|---|---|---:|---:|---:|
| `_a0b` | `_00b` 左上臂 | X | 1.0 | 0.1 |
| `_a07` | `_007` 右上臂 | X | 1.0 | 0.1 |
| `_a12` | `_012` 左大腿 | Y | 1.0 | 0.25 |
| `_a0e` | `_00e` 右大腿 | Y | 1.0 | 0.25 |
| `_a0a` | `_00a` 左肩 | X | 1.0 | 0.0 |
| `_a06` | `_006` 右肩 | X | 1.0 | 0.0 |

`Qoffset` 由可选的局部欧拉 offset 生成；上表中的基础分配操作 offset 为零。把静止 source 四元数代入该公式后，上表 target 与 `.skeleton` 静止四元数的误差小于 `1e-7`。少数同类型记录还叠加了未解的 corrective 属性，不能只看类型哈希就直接套用基础公式。

### Twist 提取 (`0x61D80537`)

只提取 source 沿指定骨轴的 twist：

```text
Qtarget = pow(extractTwist(Qsource, axis), rate) * Qoffset
```

典型参数：

| Target | Source | 骨轴 | rate | 作用 |
|---|---|---:|---:|---|
| `_a30` | `_00b` | X | -0.5 | 抵消已经继承的一半上臂 twist |
| `_a0c` | `_00c` | Y | 0.5 | 分配一半前臂 twist |
| `_a0d` | `_00d` | X | 1.0 | 跟随手腕 twist |
| `_a32` | `_012` | Y | -0.5 | 大腿 counter-twist |
| `_a13` | `_013` | X | 0.5 | 分配一半小腿 twist |
| `_a14` | `_014` | Y | 1.0 | 跟随脚踝 twist |

offset X/Y/Z 的字段哈希分别为 `0x597EA425`、`0x2E7994B3`、`0xB770C509`。把 offset 纳入后，`pl1400` 中全部 22 条该类型记录都能重建静止 target 四元数，误差小于 `2e-7`；忽略 offset 会让部分骨骼方向错误。

## 5. Corrective 操作

其余 5 类操作使用 source 旋转角度驱动关节附近的 corrective 骨：

| 类型哈希 | 已确认特征 |
|---|---|
| `0x426305E9` | 单方向姿态修正，包含输入/输出方向、角度范围和强度 |
| `0x448815BE` | 双分量 swing 旋转修正，核心求值路径已由 EXE 确认 |
| `0x6BE3DB64` | swing 驱动的 target 局部缩放修正，核心求值路径已由 EXE 确认 |
| `0xA0530081` | 另一种多参数关节修正 |
| `0x419A6851` | 主要用于头部周围 `_a42.._a4d` 的修正骨 |

这些操作影响肘、膝、肩胯和头部的局部体积。只实现两类核心 twist 操作可以先消除大臂、大腿的严重分离，但要完整复现游戏姿态仍需按 SOP 顺序实现 corrective 操作。

### 双分量 Swing 旋转修正 (`0x448815BE`)

游戏本体在统一 SOP 求值函数 `0x142839400..0x14283C3C4` 中直接分派该哈希。已确认的基础字段如下：

| 字段哈希 | 类型 | 行为 |
|---|---|---|
| `0x218365B8` | integer | 跟踪轴枚举：`0=X`、`1=Y`、`2=Z` |
| `0xFDE1DC21` | float | 第一正交 swing 分量倍率 |
| `0x020E2107` / `0x75091191` | float | 第一分量下限/上限，文件值为度，运行时乘 `pi/180` |
| `0x3A601413` | float | 第二正交 swing 分量倍率 |
| `0x7AC1F875` / `0x0DC6C8E3` | float | 第二分量下限/上限，文件值为度，运行时乘 `pi/180` |
| `0x597EA425` / `0x2E7994B3` / `0xB770C509` | float | 最终局部欧拉 offset X/Y/Z |

运行时把 source 欧拉角转为四元数，旋转所选基轴，计算该轴相对基准的 swing 夹角和方位。方位被映射为两个带符号的正交分量；每个分量分别乘倍率、按自己的角度区间夹紧，再合成为 target 局部旋转。随后执行可选的逐轴欧拉限幅，并叠加 offset。

可选欧拉限幅字段为：下限 X/Y/Z `0x82BEF4AE`、`0xF5B9C438`、`0x6CB09582`，上限 X/Y/Z `0x903510C1`、`0xE7322057`、`0x7E3B71ED`。它们不是所有记录都存在，缺失时必须保持原运行路径的无界行为。

### Swing 驱动缩放修正 (`0x6BE3DB64`)

该操作不写 target 旋转。它根据 source 的 swing 生成一个带符号驱动量，乘响应向量得到 XYZ 缩放增量，逐轴夹紧后写入：

```text
delta = clamp(response * swingDriver, lower, upper)
target.localScale = max(1 + delta, 0.001)
```

| 字段哈希 | 类型 | 行为 |
|---|---|---|
| `0x2E933545` / `0x599405D3` / `0xC09D5469` | float | 归一化后的被跟踪局部轴 X/Y/Z |
| `0x661B9B76` / `0x111CABE0` / `0x8815FA5A` | float | 输出缩放响应向量 X/Y/Z |
| `0xF852F4E8` | integer | 驱动曲线模式；当前玩家样本为 `0` 或 `3` |
| `0xF77F6A12` / `0xE5F48E7D` | float | X 增量下限/上限 |
| `0x80785A84` / `0x92F3BEEB` | float | Y 增量下限/上限 |
| `0x19710B3E` / `0x0BFAEF51` | float | Z 增量下限/上限 |

夹在六个限幅值之间的整数属性 `0x880868A4`、`0x9A838CCB`、`0xFF0F5832`、`0xED84BC5D`、`0x66060988`、`0x748DEDE7` 在当前游戏 EXE 中没有哈希常量出现，求值函数也不读取它们。它们仍属于原始记录契约，编辑和回写时必须原样保留，不能据此推断当前运行时开关。

`pl1400` 的 `_a58 <- _012` 与 `_a59 <- _00e` 都先执行一条 `0x448815BE`，再执行一条 `0x6BE3DB64`。其缩放响应分别只落在局部 Y，区间为 `[-1, 0]`，因此效果是随大腿 swing 只缩短、不放大物理根骨的局部 Y。

## 6. SOP 与 Cloth 可以叠加

对完整玩家数据的 35 个 SOP、2,383 条操作与 `research_output/cloth/all_clp.json` 做骨 ID 关联后，得到 16 条 SOP-to-CLP 重叠记录、8 根唯一 target；全部都是 CLP 链根节点，不是链中间节点。

| 模型 | CLP 根 target | SOP 类型 | 来源 |
|---|---|---|---|
| `pl1400` | `_a58`、`_a59` | 每根各一条 `0x448815BE` + `0x6BE3DB64` | 左/右大腿 `_012`、`_00e` |
| `pl1800` | `_ac2.._ac7` 六根 | 每根一条 corrective，并各叠一条 `0xB1FFF4E6` | 左/右大腿 `_012`、`_00e` |

这直接排除了“某个特殊 SOP 类型才允许物理”的假设。普通已实现的 `0xB1FFF4E6` Swing/Twist 也能直接驱动 CLP 根骨；SOP 负责物理输入骨的动画姿态或局部体积修正，CLP 随后继续模拟其子链。可复现扫描脚本是 `scripts/research/analyze_sop_cloth_overlap.py`，全量字段分布脚本是 `scripts/research/analyze_sop_properties.py`。

## 7. 正确求值顺序

```text
读取 skeleton 静止局部 TRS
  -> 采样 MOT，覆盖正常动画骨局部 TRS
  -> 按文件顺序执行 SOP，生成 deform/corrective 局部姿态
  -> 按 skeleton 父级计算所有世界矩阵
  -> inverse bind * posed world
  -> GPU 顶点蒙皮
```

`.skeleton` 中已经保存 deform 骨的静止结果，因此编辑器无动画时直接使用原始 rest pose。解析器仍会在内部把“静止 source 代入 SOP”的结果与静止 target 比较；这是一项兼容性自检，用于判断四元数乘法顺序、骨轴、倍率和当前操作变体是否已经正确解析。只有通过自检的操作才进入动画求值。

## 8. 骨架 Mod 约束

- 保留原始 `_xxx` 骨名，尤其是 `_aXX` deform/corrective 骨和它们的 source 骨。
- `.sop` 按骨名编码引用，不按 skeleton 数组下标引用；改变数组顺序通常可以工作，改名或删除引用骨则会断开操作。
- 只替换 mesh、minfo 和 skeleton 时，游戏仍会读取原始 `.sop`；新骨架必须继续满足原 SOP 的 target/source 契约。
- Blender 插件会在内存中编辑 SOP，并只在“单独导出 SOP”或主体显式导出时写入 unpack；恢复 source、修改属性和预览本身不写文件。
- Blender 当前只近似预览已确认的基础 Swing/Twist。`0x448815BE` 与 `0x6BE3DB64` 会被完整保留并回写，但尚不在 Blender 内精确求值。
- `deform_bone_boundary_box` 是 minfo 中的包围盒数据，不负责计算 deform 骨姿态。

当前 `pl1400` LOD0 有 21,046 个顶点使用 SOP target 骨，占全部顶点 37.4%，承担约 15.7% 的总蒙皮权重。因此 SOP 不是可忽略的少量辅助效果，而是角色蒙皮的基础组成部分。

## 9. 当前编辑器支持范围

编辑器已经独立解析 `.sop`，并在身体动画采样后执行通过静止姿态自检的 swing-twist 分配和 twist 提取操作。未知的 5 类 corrective 操作，以及与额外 corrective 属性混合、无法通过基础公式自检的变体，目前会被保守跳过。结果是大臂、大腿等主 deform 链已经跟随动画，但关节局部体积还不能视为与游戏完全一致。

`pl1400.sop` 当前状态不是“101 条都已实现”：

| 范围 | 记录数 | 探明状态 | 预览器 |
|---|---:|---|---|
| Swing/Twist 分配与 Twist 提取 | 38 | 核心公式已确认；少数混合变体仍受 rest-pose 自检保护 | 通过自检后执行 |
| 其余 5 类 Corrective | 63 | 作用区域或参数特征部分探明，完整数学公式未确认 | 未实现 |
| 目录外类型哈希 | 当前为 0 | 未探明 | 未实现 |

探索器会把每个 `.sop` 的逐骨清单写进 `manifest.md`，并把文件序号、source/target、类型哈希、状态和全部原始属性写进 `workspace.json` 的 `SkeletonConstraints`。C++ 编辑器的骨架面板提供同样的 `SOP 约束` 检查页，可以按骨骼、类型名称、哈希和探明状态筛选。Blender 插件则提供基础复制旋转的新增、修改、恢复 source、单独导出 SOP 和随主体导出；未知或尚未实现的公式保持只读但字节级保留。

操作元数据集中在 `_lib/sop_operations_zh.json`。以后确认新类型时，先在该目录增加或更新名称、用途与 `Discovery` 状态，再实现求值器并把 `Runtime` 改为已支持。目录中没有的哈希必须显示为“未探明”，原始属性仍保留；不能静默跳过，也不能因为已经知道大致作用就标成“已探明”。
