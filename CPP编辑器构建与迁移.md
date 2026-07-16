# C++ 编辑器构建与迁移

## 构建入口

仅支持 Windows 10/11 x64。安装 Visual Studio 2022 的“使用 C++ 的桌面开发”工作负载后，不需要手工配置 PATH。

```bat
build.bat
build.bat Debug
build.bat Release
build.bat test
build.bat run
build.bat clean
```

双击或无参数运行 `build.bat` 时，脚本会增量构建 `RelWithDebInfo` 并直接启动编辑器；存在 `explore_output/manifest.md` 时会自动打开该工作区。构建失败时控制台会保留并等待按键。`build.bat RelWithDebInfo` 只构建不启动，其他带参数调用也不会暂停，适合终端和自动化调用。

输出位于 `out/bin/<配置>/GBFRModtools.exe`。依赖固定版本并缓存到 `.deps/`，不写入 `_lib/`。

`build.bat` 通过 `vswhere.exe` 自动定位 VS、MSVC、CMake 和 Ninja。本机验证路径如下，仅用于排错，脚本没有硬编码这些路径：

| 工具 | 验证路径 |
|---|---|
| Visual Studio 2022 | `D:\Microsoft Visual Studio\2022\Community` |
| MSVC 19.44 | `D:\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64\cl.exe` |
| CMake 3.31.6 | `D:\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe` |
| Ninja 1.12.1 | `D:\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe` |

VS 自带 CMake 3.31.6 在当前中文源码路径进入 `project()` 时会以 `0xC0000409` 崩溃。脚本构建期间临时把仓库映射到空闲的 `R:`，结束后自动解除。源码、依赖和输出仍在原仓库；如果 `R:` 已被占用，脚本会明确报错且不会覆盖原映射。

## 使用

程序接受 `manifest.md`、`workspace.json` 或工作区目录：

```bat
out\bin\RelWithDebInfo\GBFRModtools.exe explore_output\manifest.md
```

不传参数时会尝试打开仓库内的 `explore_output/workspace.json`。程序兼容 Version 1，保持 `source -> unpack -> build` 语义。

- 资源列表按基线 SHA-256 显示缺失和修改状态。
- `.minfo/.skeleton/.mmesh` 可原生恢复、原样写入 build，并加载 D3D11 预览。
- 预览支持轨道旋转、平移、缩放、取景、线框、骨架和 BC7/BC5 DX10 DDS。
- 点击视口骨骼或骨骼列表会过滤关联 CLH；支持单 CLH/全部 CLH、当前骨骼/全部骨骼模式。
- CLH 可编辑半径、权重和两个 offset，保存到 `unpack` 后仍由现有 cloth 构建链编码 BXM。

## M5 覆盖边界

| 功能 | 当前实现 |
|---|---|
| Version 1 加载、路径约束、SHA-256、刷新 | C++ 原生 |
| 模型文件恢复与 build | C++ 原生 |
| skeleton/minfo/mmesh LOD0 解析 | C++ 原生 |
| 网格、DDS、骨架和碰撞预览 | C++ 原生 |
| CLH/CLP 读取、CLH 常用字段编辑 | C++ 原生 |
| WTB `.texture` 封回与恢复 | 旧版构建器 |
| mmat JSON 编码、A4 快捷删除 | 旧版构建器 |
| cloth XML/BXM 编码与恢复 | 旧版构建器 |
| 从 DDS 新建 `.texture` | 旧版构建器与 `nier_cli_mgrr` |

右侧 `Migration Coverage` 面板和工作区顶部的“旧版构建器”按钮提供兼容入口。M5 是完成迁移评估和稳定边界，不代表已经删除 PowerShell；在上述四类构建全部原生对齐前，`GBFR_WorkspaceBuilder.ps1` 和 `_lib` 必须保留。

## 验证基线

`build.bat test` 包含：

- 临时 Version 1 工作区的哈希、修改检测、模型 build 和恢复。
- 有本地 `explore_output` 时，断言 pl1400 与 PowerShell 一致为 188 个候选。
- pl1400 LOD0 的 minfo、skeleton 和 mmesh 集成解析。
- pl1400 `0_0` CLH 8 条碰撞、CLP 60 个节点解析，以及临时 CLH 副本写回。
- 损坏 FlatBuffer 的越界拒绝。

测试不修改 `explore_output`；写回测试只操作系统临时目录。
