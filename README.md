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

1. 选择游戏解包目录中的角色 `.minfo`，生成 `explore_output` 工作区。
2. 或直接选择已有工作区的 `workspace.json`。
3. 只编辑 `unpack/data/`；`source/data/` 是恢复基线，`build/data/` 是 Mod 输出。

## 文档

- [文档索引](docs/README.md)
- [从源码构建与排错](docs/构建与开发.md)
- [工作区、编辑器与格式说明](docs/工作区与格式说明.md)
- [模型与骨架 Mod 流程](docs/模型与骨架Mod制作流程.md)
- [C++ 架构](docs/架构.md)

## 当前边界

C++ 架构已支持工作区生成与检测、模型文件恢复/写入 `build`、DDS/网格/骨架/碰撞预览、MOT 动画、SOP deform 和 cloth 检查编辑。WTB `.texture`、`.mmat` 和 cloth BXM 的原生编码尚未实现，当前版本不再携带旧 PowerShell 构建器。
