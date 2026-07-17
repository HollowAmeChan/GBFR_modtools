# SOP 骨骼后处理与 Deform 骨

本文记录 GBFR 角色骨架中 `_aXX` deform/corrective 骨的运行方式。它们不是普通动画骨，也不是 Blender 文件内的约束；游戏在采样 `.mot` 后，使用同模型目录下的 `.sop` 逐帧生成这些骨骼的局部姿态。

## 1. 四类资源的职责

| 资源 | 职责 |
|---|---|
| `.skeleton` | 骨骼名称、父级和静止局部 TRS |
| `.mot` | 正常动画骨的局部位移、欧拉旋转和缩放轨道 |
| `.sop` | 从正常动画骨派生 deform/corrective 骨姿态的角色级操作表 |
| `*_seq_edit_ik.bxm` | 动作时间线上的脚部 IK、踝高和角度限制开关 |

`seq_edit_ik` 不负责大臂、大腿的 deform。Blender 插件也只读取 `.skeleton` 并把其中的 TRS 应用为 rest pose，不读取 `.sop`，因此导入 Blender 后看不到游戏运行时的 deform 约束。

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
| `0x448815BE` | 带 `-179..179` 等上下限的旋转重映射 |
| `0x6BE3DB64` | 多轴或分段 corrective，同一 target 上常与其他操作叠加 |
| `0xA0530081` | 另一种多参数关节修正 |
| `0x419A6851` | 主要用于头部周围 `_a42.._a4d` 的修正骨 |

这些操作影响肘、膝、肩胯和头部的局部体积。只实现两类核心 twist 操作可以先消除大臂、大腿的严重分离，但要完整复现游戏姿态仍需按 SOP 顺序实现 corrective 操作。

## 6. 正确求值顺序

```text
读取 skeleton 静止局部 TRS
  -> 采样 MOT，覆盖正常动画骨局部 TRS
  -> 按文件顺序执行 SOP，生成 deform/corrective 局部姿态
  -> 按 skeleton 父级计算所有世界矩阵
  -> inverse bind * posed world
  -> GPU 顶点蒙皮
```

`.skeleton` 中已经保存 deform 骨的静止结果，因此编辑器无动画时直接使用原始 rest pose。解析器仍会在内部把“静止 source 代入 SOP”的结果与静止 target 比较；这是一项兼容性自检，用于判断四元数乘法顺序、骨轴、倍率和当前操作变体是否已经正确解析。只有通过自检的操作才进入动画求值。

## 7. 骨架 Mod 约束

- 保留原始 `_xxx` 骨名，尤其是 `_aXX` deform/corrective 骨和它们的 source 骨。
- `.sop` 按骨名编码引用，不按 skeleton 数组下标引用；改变数组顺序通常可以工作，改名或删除引用骨则会断开操作。
- 只替换 mesh、minfo 和 skeleton 时，游戏仍会读取原始 `.sop`；新骨架必须继续满足原 SOP 的 target/source 契约。
- Blender 插件不会导入或导出 SOP 约束。导出后的 skeleton 静止姿态正确，不代表运行时 deform 一定正确。
- `deform_bone_boundary_box` 是 minfo 中的包围盒数据，不负责计算 deform 骨姿态。

当前 `pl1400` LOD0 有 21,046 个顶点使用 SOP target 骨，占全部顶点 37.4%，承担约 15.7% 的总蒙皮权重。因此 SOP 不是可忽略的少量辅助效果，而是角色蒙皮的基础组成部分。

## 8. 当前编辑器支持范围

编辑器已经独立解析 `.sop`，并在身体动画采样后执行通过静止姿态自检的 swing-twist 分配和 twist 提取操作。未知的 5 类 corrective 操作，以及与额外 corrective 属性混合、无法通过基础公式自检的变体，目前会被保守跳过。结果是大臂、大腿等主 deform 链已经跟随动画，但关节局部体积还不能视为与游戏完全一致。
