# MOT 骨架基准与旧动画迁移

## 双模式预览契约

Blender 和 C++ 预览器中的 MOT 必须明确区分资源区，不能只切换 MOT 文件而继续沿用另一侧 skeleton rest：

| 模式 | MOT | 缺轨静止值与局部 offset | Blender 显示 |
|---|---|---|---|
| source 预览/首次导入 Action | `source/data/<fp|pl>/<id>/*.mot` | 同工作区 source `.skeleton` | 映射到当前 Blender rest |
| 导出预览/导回 Action | `unpack/data/<fp|pl>/<id>/*.mot` | 同工作区 unpack `.skeleton` | 映射到当前 Blender rest |

MOT 面板常驻提示“修改骨架后，请先导出到工作区再编辑或导出动画”，但不阻断 Action 的导入、编辑、验证、导出或回导。修改 head、方向或 Roll 后，应由用户先重新导出模型，确保 Blender 当前 rest、unpack skeleton 与后续 MOT 使用同一基准。

## 旧实现的问题

旧 Blender MOT 路径始终使用导入模型时保存的 `gbfr_rest_*`，普通播放也固定读取 source MOT。脸骨 rest 已经修改并导出到 unpack 后，C++ 预览器使用新 unpack skeleton，而 Blender 仍在旧 source offset 下显示 Action；用户按这个画面完成的编辑写回后，游戏与 Blender 不一致。

旧 Action 不应通过简单的 `new_rest @ inverse(old_rest)` 直接转换。官方 MOT 含非均匀缩放，矩阵换基可能产生 MOT 的位置/XYZ 欧拉/缩放轨道无法表示的剪切。可复现迁移脚本为 [rebase_mot_rest.py](../../scripts/research/rebase_mot_rest.py)，步骤是：

1. 用 source MOT/source skeleton 重建旧插件的精确基线与 Blender TRS 投影。
2. 从旧 unpack MOT 反推出用户相对该投影做出的 Action 编辑量。
3. 把恢复的 Action basis 应用到新 unpack rest，并按当前导出器相同规则投影为 MOT TRS。
4. 保持原 MOT 的帧数、轨道顺序、骨号、属性和未知字段，写入独立目录并重新解析验证。

## fp1400 一次性修复记录

2026-07-28 对奶刀工作区 `unpack/data/fp/fp1400` 中相对 source 发生变化的 27 个 MOT 执行了上述迁移。原文件备份在未提交目录 `research_output/mot_rest_rebase/original`，候选与清单位于 `research_output/mot_rest_rebase/converted`；安装后 27/27 个目标 SHA-256 与候选一致，逐帧重建最大矩阵误差为 `1.7881393e-7`。

`fp1400_e08a.mot` 和 `fp1400_e09a.mot` 的 `_840` 在部分帧包含零轴/非均匀缩放。rest 改变后产生的剪切无法原样写入 MOT，两条文件使用与插件导出器相同的最近 TRS 投影，必须重点做 Blender 导出预览与游戏目测；其余迁移文件的最大投影误差不超过约 `1.5e-5`。
