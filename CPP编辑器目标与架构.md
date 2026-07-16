# GBFR C++ 编辑器目标与架构

> 实施状态（2026-07-17）：M0-M5 已完成首轮实现。C++ 已覆盖工作区、模型解析/构建、D3D11 预览、骨架与 CLH/CLP 联动，并新增 `.mot` 独立解码器、骨骼姿态采样、GPU 顶点蒙皮和动画切片时间轴；texture、mmat、BXM 编码和新建 texture 继续走旧版兼容入口。具体边界与验证见 [CPP编辑器构建与迁移.md](CPP编辑器构建与迁移.md)。

本文确定下一代 GBFR Modtools 的技术选型、代码边界、构建入口、目录职责和第一阶段模型预览目标。开始搭建 C++ 工程前，以本文作为实现约束。

## 1. 已确定的方向

- 正式桌面程序使用 **C++20**，Python 只用于格式研究、测试数据生成和迁移脚本。
- UI 使用 **Dear ImGui Docking**。
- 程序仅支持 Windows 10/11，窗口与输入使用 **Win32**，渲染使用 **Direct3D 11**。
- GBFR 的 `.minfo/.mmesh/.skeleton` 使用自建解析器，不经 Assimp 转换。
- **Assimp** 只负责以后导入/导出 FBX、glTF、OBJ 等通用格式，并转换到工具内部统一模型。
- 完整 `unpack` 是正确工作流，不改变 `source -> unpack -> build` 语义。重构目标是拆分代码职责，不是减少工作区文件。
- 新程序首先兼容现有 `workspace.json` Version 1；确认需要新增字段后再设计 Version 2 迁移。
- PowerShell 工具在 C++ 功能对齐前继续保留，不能一开始删除。

## 2. 第一阶段产品目标

第一阶段交付一个能实际替代部分 WinForms 工具的编辑器外壳：

1. 打开现有 `manifest.md` 或 `workspace.json`。
2. 列出工作区资源、修改状态、构建目标和缺失输入。
3. 支持现有模型二进制的拖入、恢复和原样构建。
4. 读取一组同名 `.minfo + .mmesh + .skeleton`。
5. 在 ImGui 中显示可停靠的资源列表、属性面板、日志和 3D 视口。
6. 3D 视口显示静态网格、基础色贴图和骨架。
7. 点击骨骼或骨骼列表时同步选择，并显示该骨骼关联的 CLH 碰撞数据。
8. 保留当前 skeleton 碰撞编辑器已有的文件模式与总体模式。

第一阶段明确不做：

- 不复刻 GBFR 完整 Shader。
- 不复刻游戏动画状态机、切片混合、IK、面部专用通道或动画驱动的 cloth；单个 `.mot` 骨骼切片已经可以按 60 FPS 预览。
- 不在程序内修改网格、刷权重或编辑骨架静止姿态。
- 不导出 `.mmesh/.minfo/.skeleton`；模型编辑仍由 Blender 插件完成。
- 不实现跨平台渲染后端。
- 不解析 LOD1-3 作为编辑目标；预览默认使用 LOD0。
- 不立即重写全部 texture、Granite、mmat 和 cloth 构建逻辑。

## 3. 本机开发工具路径

当前开发机已验证以下路径。路径用于诊断和兜底，未来 `build.bat` 必须优先自动发现，不允许要求用户手工配置 PATH。

| 工具 | 当前版本 | 当前路径 |
|---|---:|---|
| Visual Studio | Community 2022 17.14 | `D:\Microsoft Visual Studio\2022\Community` |
| vswhere | VS Installer | `C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe` |
| VsDevCmd | VS 2022 | `D:\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat` |
| vcvars64 | VS 2022 | `D:\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat` |
| MSVC cl.exe | 19.44.35226 | `D:\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64\cl.exe` |
| CMake | 3.31.6-msvc6 | `D:\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe` |
| Ninja | 1.12.1 | `D:\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe` |
| Git | 2.48.1 | `C:\Program Files\Git\cmd\git.exe` |
| Python | 3.13.9 | `C:\Users\hhh12\AppData\Local\Programs\Python\Python313\python.exe` |
| 中文字体 | Microsoft YaHei | `C:\Windows\Fonts\msyh.ttc` |

Python 不是编译 C++ 程序的必要条件。只有执行 schema 生成、测试 fixture 或迁移脚本时才使用上述 Python。

## 4. `build.bat` 契约

下一步创建根目录 `build.bat`，使用者只需要双击或从终端执行：

```text
build.bat                    默认 RelWithDebInfo
build.bat Debug
build.bat Release
build.bat clean
build.bat test
build.bat run
```

`build.bat` 必须完成：

1. 从 `%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe` 查找带 C++ x64 工具链的最新 VS。
2. 调用该实例的 `VsDevCmd.bat -arch=x64 -host_arch=x64`。
3. 优先使用该 VS 自带的 CMake 和 Ninja；找不到时再查 PATH。
4. 检查 CMake、Ninja、cl.exe 和 Git，并输出最终采用的绝对路径与版本。
5. 使用 `CMakePresets.json` 配置到 `out/build/<preset>/`。
6. 第一次构建时自动获取固定 commit 的依赖；后续使用本地 `.deps/` 缓存。
7. 构建到 `out/bin/<config>/GBFRModtools.exe`。
8. `test` 调用 CTest；`run` 只在构建成功后启动程序。
9. 任一步失败都返回非零退出码，并停在明确错误信息上。

工具发现不能在 CMakeLists 中硬编码本机 `D:` 路径。本节的绝对路径只作为当前机器的已验证兜底与排错依据。

## 5. 第三方库

| 库 | 职责 | 接入方式 |
|---|---|---|
| Dear ImGui Docking | 编辑器 UI、DockSpace、表格、属性控件 | 固定 commit，编译 Win32/DX11 backend |
| Assimp | FBX/glTF/OBJ 等通用格式交换 | CMake FetchContent，初期可关闭 |
| FlatBuffers | `.minfo/.skeleton/.mmat` schema 访问 | 固定版本，生成 C++ 头 |
| DirectXTex | DDS 读取、BCn 解码、mip 与 GPU 上传辅助 | 固定版本 |
| GLM | 矩阵、四元数、坐标变换 | 头文件库 |
| nlohmann/json | `workspace.json` 和会话状态 | 头文件库 |
| pugixml | cloth XML 读取与安全写回 | 静态库 |
| spdlog | 文件日志与 ImGui 日志面板 | 静态库 |
| Catch2 | 单元测试和格式回归测试 | 仅测试目标 |

第一阶段不引入 ECS、渲染图、脚本运行时或大型 Editor Framework。后台任务先使用 `std::jthread`、队列和取消标记，避免过早引入任务系统。

依赖必须固定 tag 或 commit，禁止每次构建跟随 `master`。下载目录统一为 `.deps/`，不放入 `_lib/`；`_lib/` 继续表示旧工具运行时二进制。

参考：

- Dear ImGui：<https://github.com/ocornut/imgui>
- Assimp：<https://github.com/assimp/assimp>
- DirectXTex：<https://github.com/microsoft/DirectXTex>
- FlatBuffers：<https://github.com/google/flatbuffers>

## 6. 新代码目录职责

```text
GBFR_modtools/
  CMakeLists.txt
  CMakePresets.json
  build.bat

  cmake/
    Dependencies.cmake
    CompilerOptions.cmake

  src/
    gbfr_core/
      asset/                 与文件格式无关的资产数据模型
      workspace/             工作区读取、哈希、修改状态、依赖关系
      build/                 构建、恢复、导入命令
      jobs/                  后台任务、进度、取消
      io/                    路径约束、原子写入、日志

    gbfr_formats/
      minfo/                 FlatBuffers ModelInfo
      skeleton/              FlatBuffers 骨架与名称映射
      mmesh/                 顶点流、权重流、索引流
      mmat/
      cloth/
      texture/
      granite/

    gbfr_render/
      d3d11/                 Device、SwapChain、Shader、GPU 资源
      scene/                 PreviewScene 与可见对象
      camera/                轨道相机与 framing
      picking/               网格/骨骼/碰撞拾取
      debug_draw/            骨架线、关节、碰撞几何、网格线框

    gbfr_exchange/
      assimp/                aiScene 与内部 Asset 的双向适配

    gbfr_editor/
      app/                   Win32 生命周期与主循环
      panels/                Workspace、Inspector、Viewport、Log
      dialogs/               打开工作区、确认覆盖、错误详情
      commands/              UI 发出的可撤销业务命令
      selection/             跨面板选择状态

  include/gbfr/              跨 target 的公开头文件
  assets/shaders/            HLSL
  assets/icons/
  schemas/flatbuffers/
  tests/unit/
  tests/integration/
  tests/fixtures/
  scripts/python/
  docs/formats/
  legacy/powershell/         C++ 对齐后再迁入，初期保持原位置
  .deps/                     自动下载缓存，Git 忽略
  out/                       编译输出，Git 忽略
```

对应 CMake target：

```text
gbfr_core
gbfr_formats
gbfr_render
gbfr_exchange              可选
gbfr_editor                最终 EXE
gbfr_tests
```

依赖方向必须单向：

```text
gbfr_editor -> gbfr_render -> gbfr_core
gbfr_editor -> gbfr_formats -> gbfr_core
gbfr_exchange -------------> gbfr_core
```

`gbfr_core` 和 `gbfr_formats` 不能引用 ImGui、D3D11 或 Win32 UI 类型。UI 不能直接修改 JSON/XML/二进制文件，只能调用 core command/service。

## 7. 工作区保持不变

```text
<workspace>/
  manifest.md
  workspace.json
  source/data/              原始恢复基线
  unpack/data/              完整可编辑资源
  build/data/               最终 Mod 输出

  .gbfr/
    cache/                  可删除的预览派生缓存
    logs/
    temp/
    session.json            当前选择、相机和面板状态
    imgui.ini
```

- `source/unpack/build` 的现有含义不变。
- `.gbfr` 只存程序内部状态，删除后不能损坏 Mod 工程。
- 预览缓存不能写进 `unpack`，也不能被构建到 Mod。
- 新程序允许打开任意目录下的工作区，不再假定名字必须是 `explore_output`。
- Version 1 工作区必须只读加载成功；新增字段应有默认值。

## 8. 内部资产模型

格式解析完成后必须转换成稳定的内部数据，渲染器不直接持有 FlatBuffers 对象或 XML 节点。

```text
SkeletonAsset
  bones[]: name, displayName, parent, localTransform, worldTransform

MeshAsset
  vertices[]: position, normal, tangent, uv, jointIndices, jointWeights
  indices[]
  submeshes[]: firstIndex, indexCount, materialIndex

ModelInfoAsset
  lods[], chunks[], submeshes[], materials[], bonesToWeightIndices[]

MaterialAsset
  texture slots, fallback colors, source mmat references

TextureAsset
  dimensions, format, mip data, color space

CollisionSet
  source CLH, id, p1, p2, offsets, radius, weight, capsule flags
```

Assimp 的 `aiScene` 也必须转换到上述 `MeshAsset/SkeletonAsset`，不能让 Assimp 类型扩散到 renderer 和 editor。

## 9. 参考 Blender 插件的最小解析流程

本机参考实现：

```text
C:\Users\hhh12\AppData\Roaming\Blender Foundation\Blender\4.5\scripts\addons\io_gbfr_blender_tools\gbfr_import.py
```

### 9.1 skeleton

1. FlatBuffers 读取骨骼数组。
2. 每根骨骼读取 `ParentId/Name/Position/Quaternion/Scale`。
3. `65535` 表示无父骨骼。
4. 原始变换为相对父骨骼的 local transform，按层级计算 world transform。
5. 保留 `_xxx` 原名，同时应用现有 `humanoid_bone_names.json` 显示名。
6. 骨骼默认画成父子连线和小关节点，不模仿 Blender 的八面体骨骼。

### 9.2 minfo

使用 `MInfo_ModelInfo.fbs` 读取：

- `lods[0]`
- `mesh_buffers`
- `chunks`
- `vertex_count`
- `poly_count_x3`
- `buffer_types`
- `sub_meshes`
- `materials`
- `bones_to_weight_indices`

第一阶段只读取 LOD0，并对所有 offset、size、count 做文件边界检查。

### 9.3 mmesh

按 Blender 插件当前实现解析：

- 主顶点流：`float3 position`。
- normal：3 个 half，加 2 字节 padding。
- tangent：3 个 half，加 2 字节 padding；需要核对 Y/Z 重排。
- UV：2 个 half。
- `buffer_types & 2` 时读取 4 个 `uint16` 权重索引，再经 `bones_to_weight_indices` 映射到 skeleton bone index。
- `buffer_types & 8` 时读取 4 个 `uint16` 权重并除以 65535。
- 最后一个 mesh buffer 为 `uint32` 三角形索引；插件导入时交换首尾索引修正绕序。
- chunk 的 `offset/count` 按索引数量记录，用于拆分 submesh 和 material。

不能无条件相信插件中的 seek 顺序。C++ 解析器以 minfo 的 `mesh_buffers[].offset/size` 为准，并用实际文件生成回归测试。

### 9.4 坐标系

Blender 插件最终对 armature 绕 X 轴旋转 `+90°`，用于把游戏 Y-up 转为 Blender Z-up。编辑器内部统一采用右手 Y-up；只在相机和 D3D shader 矩阵边界做转换。坐标、法线、切线、骨架和碰撞必须共用同一转换函数，禁止各模块各写一套轴交换。

## 10. 最小贴图预览

第一阶段不复刻材质 Shader，只实现足以辨认模型的预览材质：

1. 优先寻找与 submesh/material 对应的 `albd` DDS。
2. albd 按 sRGB 采样。
3. 找到 `nrml` 时增加切线空间法线；找不到则使用顶点法线。
4. 普通 `msk1/msk2` 仍只作为调试数据；面部 `A7=5` 透明覆盖材质明确使用 `msk2.B` 作为眉毛/睫毛覆盖率。
5. 无贴图时使用按 material index 区分的稳定调试颜色。
6. 使用单方向光、环境光、网格线框切换和双面调试开关。

贴图关联先读取工作区中已有 DDS 和 mmat `A2.Name`；Granite A4 的完整运行时材质还原不是第一阶段阻塞项。

### 10.1 GPU 预览管线

预览器的 HLSL 独立存放于 `assets/shaders/preview.hlsl`，构建后复制到可执行文件旁的 `shaders/`。渲染器不再内嵌 shader 字符串，shader 编译错误会直接指向源文件。

- CPU 解码 `.mot` 并计算骨骼层级、局部/世界姿态与 inverse-bind 组合矩阵。
- 最多 512 个转置后的蒙皮矩阵通过 `b1` 常量缓冲上传；顶点位置和法线在 vertex shader 中按四骨权重蒙皮。
- `b0` 保存视图投影、材质颜色、光照模式和透明阈值；`t0-t3` 保存基础色及附加颜色层/遮罩。
- opaque pass 开启深度写入；`A7=5` alpha-overlay pass 关闭深度写入、采样 `msk2.B`、执行 alpha clip，并施加稳定的深度偏移；骨架和碰撞 debug pass 关闭深度测试以保持前置显示。
- 网格顶点缓冲为不可变 GPU 资源。切换动画只更新骨骼常量缓冲和调试骨架，不复制或重建整份网格。

512 根骨骼对应 32 KiB 常量数据，低于 D3D11 单常量缓冲限制；超过该上限的模型必须明确拒绝加载，不能静默截断。

## 11. 视口与交互验收

必须具备：

- 轨道旋转、平移、缩放、自动 framing。
- 实体/线框/法线调试模式。
- 网格、骨架、碰撞分别显示/隐藏。
- 骨架始终在网格前方显示的 X-Ray 选项。
- 点击骨骼选择；列表选择同步高亮视口骨骼。
- 选择骨骼后显示所有来源 CLH 碰撞。
- 碰撞球/段显示，选中项与属性表联动。
- 重新拖入 `.mmesh/.minfo/.skeleton` 后只重载受影响的预览资源。
- 格式错误显示文件、offset、期望值和实际值，不能只显示“加载失败”。

## 12. 实施里程碑

### M0：工程骨架

- `build.bat`、CMakePresets、依赖固定。
- 空 Win32 + D3D11 + ImGui DockSpace。
- 日志、崩溃边界、CTest。

### M1：工作区对齐

- 读取现有 workspace Version 1。
- 列表、哈希、修改检测、恢复、原样模型构建。
- 用当前 pl1400 验证候选数量与 PowerShell 一致。

### M2：模型数据

- FlatBuffers minfo/skeleton。
- mmesh 顶点、索引、submesh、权重解析。
- 与 Blender 插件导入结果比较顶点数、三角形数、骨骼数和材质分组。

### M3：最小预览

- D3D11 网格渲染、DDS albd、骨架 debug draw。
- 相机、framing、显隐和拾取。

### M4：碰撞联动

- 读取 CLH/CLP。
- 骨骼选择、碰撞可视化、当前文件/全部文件模式。
- 编辑写回仍通过 core command，支持恢复与构建。

### M5：迁移评估

- 对比 C++ 与 PowerShell 功能覆盖。
- 未覆盖部分继续调用旧 `_lib` 工具。
- 达到对齐后才移动脚本到 `legacy/powershell`。

## 13. 第一阶段完成条件

以当前 pl1400 工作区为基准：

- 双击 `build.bat` 能从未配置 PATH 的普通终端完成构建。
- 程序能打开当前工作区且不修改任何文件。
- 读取 pl/fp/wp 的 minfo、skeleton、LOD0 mmesh。
- pl1400 预览的顶点数、三角形数、骨骼数与 Blender 插件一致。
- 能看到 albd 或稳定调试材质。
- 骨架与网格位置一致。
- 能选中骨骼并查看该骨骼的 CLH 碰撞记录。
- 所有 parser 都有损坏文件、越界 offset 和正常样本测试。
- 关闭程序后只允许 `.gbfr/session.json`、日志和缓存发生变化。
