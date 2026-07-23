# GBFR Modtools

Windows 上的《碧蓝幻想：Relink》角色资源工作区、模型预览与 Mod 编辑器。主程序使用 C++20、Dear ImGui 和 Direct3D 11。

## 开始

需要 Windows 10/11、Git for Windows，以及 Visual Studio 2022 的“使用 C++ 的桌面开发”工作负载。

```bat
git clone https://github.com/HollowAmeChan/GBFR_modtools.git
cd GBFR_modtools
build.bat
```

双击 `build.bat` 也可以。首次运行会自动下载并校验 C++ 依赖与工作区解码工具，然后编译并启动编辑器。

进入编辑器后：

1. 选择游戏解包目录中的角色 `.minfo`，并按需指定工作区输出目录；留空则生成到默认的 `explore_output`。
2. 或直接选择已有工作区的 `workspace.json`。
3. 每个输出目录都是可独立保存和再次打开的工程。只编辑其中的 `unpack/data/`；`source/data/` 是恢复基线，`build/data/` 是 Mod 输出。

Blender 插件导出的 `.minfo/.skeleton/.mmesh` 也应覆盖到 `unpack/data/` 的对应原路径。角色工作区会登记 `model_streaming/lod#` 与 `shadowlod#` 中存在的全部流式网格；编辑器点击具体 `.mmesh` 时会预览对应普通/阴影 LOD，并识别多 Mesh 分段、4/8 权重、UV1 与顶点色。编辑器还会预览材质、DDS、角色 `UI-image` 与 cloth 中间态；“贴图库”分页可按类型、名称或路径过滤并同时浏览工作区中的全部 DDS。MOT 动画和 SOP 约束仍从只读的 `source` 加载。预览器只读取 `unpack`，`build` 只作为最终 Mod 输出。

选中资源后，右侧“快捷操作”页集中提供写入与恢复：普通贴图/UI-image 使用 source WTB 模板封回，`新贴图` 使用 `nier_cli_mgrr 1.3.0_mgrr` 从 DDS 新建单槽 `.texture`，mmat JSON 使用 `flatc` 编码为 `.mmat`。mmat 快捷操作还可清除当前文件所有 `A4` 流式引用；清除后必须同时构建 `A2.Name` 对应的 2k/4k 普通贴图。所有写入都只进入 `build`，不会修改 `source`。

## 文档

- [文档索引](docs/README.md)
- [从源码构建与排错](docs/构建与开发.md)
- [工作区、编辑器与格式说明](docs/工作区与格式说明.md)
- [模型与骨架 Mod 流程](docs/模型与骨架Mod制作流程.md)
- [C++ 架构](docs/架构.md)

## 当前边界

C++ 架构已支持工作区生成与检测、模型文件、WTB/UI-image、新建 `.texture`、mmat JSON 写入/恢复 `build`、DDS/网格/骨架/碰撞预览、MOT 动画、SOP deform 和 cloth 检查编辑。cloth BXM 编码仍由 Blender 插件负责，当前版本不再携带旧 PowerShell 构建器。
