# KY-DIMM

基于 Qt、OpenCV 和 Basler pylon 的单望远镜、单相机双星点大气参数测量软件。

KY-DIMM 面向真实相机采集场景：从相机获取北极星图像，定位两个星点，在 ROI 内计算质心并形成差分样本，最终计算并显示 `r0`、`seeing`、`theta0` 和 `tau0` 等大气参数。

> 当前项目处于持续开发和硬件联调阶段。仓库中的静态测试用于保护源码契约；真实相机、光路和网络环境仍需现场验证。

## 功能概览

- 连接、断开 Basler 相机，并独立控制采集生命周期。
- 支持全画幅预览、双星点定位和 `StarA` / `StarB` 软件 ROI 跟踪。
- 在 ROI 内计算星点质心，形成双星差分测量样本。
- 按 DIMM 一致的计算流程输出：
  - `r0`：大气相干长度
  - `seeing`：视宁度
  - `theta0`：等晕角
  - `tau0`：大气时间常数
- 实时显示图像、ROI、测量参数、有效样本进度和设备状态。
- 将结果和运行元数据保存为 CSV / JSON。
- 在 Release 构建后自动收集 Qt、OpenCV、pylon GigE 和 MSVC 运行时，生成可复制的测试包。

## 系统架构

```text
Basler Camera
     │
     ▼
PylonCamera ── CameraWorker ── FrameQueue ── MeasurementWorker
                                                   │
                 DisplayMailbox ◄─────────────────┤
                       │                           │
                       ▼                           ▼
                 MainWindow                 ResultWriter
                       │                           │
                       └──── SettingsDialog ──────┘
```

主要模块职责如下：

| 模块 | 职责 |
| --- | --- |
| `PylonCamera` | 封装 Basler pylon SDK，负责相机节点、曝光、触发、AOI 和取帧 |
| `CameraWorker` | 管理相机连接、采集启停和采集线程 |
| `FrameQueue` | 在采集线程和测量线程之间传递帧数据 |
| `TwoStarTracker` | 全画幅双星定位、星点身份保持、软件 ROI 跟踪和硬件 AOI 请求 |
| `CentroidEngine` | 根据图像处理参数计算星点质心 |
| `MeasurementWorker` | 组织测量流程、差分样本和大气参数计算 |
| `AtmosphereCalculator` | 计算 `r0`、`seeing`、`theta0` 和 `tau0` |
| `DisplayMailbox` | 为 UI 提供 latest-only 的预览帧通道 |
| `ResultWriter` | 写出 CSV / JSON 结果和运行元数据 |
| `MainWindow` / `SettingsDialog` | 主界面、状态反馈和参数配置 |

## 环境要求

构建目标为 Windows x64，当前工程配置依赖以下组件：

- Visual Studio 2022，包含 MSVC 和 Windows SDK
- CMake 3.21 或更高版本
- Qt 6.11.0，MSVC 2022 64-bit
- OpenCV 4.12.0
- Basler pylon 12.2.1 及其 CMake 开发包
- Python 3.10+ 和 `pytest`，用于运行源码级测试
- 兼容的 Basler 相机；当前默认硬件型号为 `acA1920-40gm`

> `CMakeLists.txt` 中保留了开发机默认路径，但这些路径属于本地环境配置。换到其他电脑时，请通过 CMake 参数覆盖它们，不要把 SDK 或 DLL 提交到仓库。

## 构建

在 **x64 Native Tools Command Prompt for VS 2022** 或已配置 MSVC 环境的 PowerShell 中执行：

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
  -DKY_DIMM_QT_ROOT="E:/Softwoare/Qtool/qt/6.11.0/msvc2022_64" `
  -Dpylon_DIR="E:/Softwoare/Basler pylon/Development/CMake/pylon" `
  -DKY_DIMM_PYLON_RUNTIME_ROOT="E:/Softwoare/Basler pylon/Runtime/x64" `
  -DKY_DIMM_OPENCV_ROOT="E:/Softwoare/OpenCV/opencv4120"
```

如果 CMake 无法自动定位 Visual Studio 的 MSVC 运行库，可以额外指定：

```powershell
-DKY_DIMM_MSVC_REDIST_ROOT="<path-to-Microsoft.VC143.CRT>"
```

构建 Release：

```powershell
cmake --build build --config Release
```

构建完成后，程序位于：

```text
build/Release/KY_DIMM.exe
```

## 测试

当前测试主要覆盖源码级契约、物理计算、ROI 状态机、中文界面文案、部署配置和计划文档静态约束：

```powershell
python -m pytest -q
```

这些测试不等同于硬件验收。涉及真实测量结果时，还需要使用目标相机、实际光路和目标网络配置进行联机测试。

## Release 部署

Windows Release 构建会执行 `cmake/KY_DIMMDeploy.cmake`，将运行所需文件放到 `build/Release`，包括：

- Qt DLL 和插件
- 对应配置的 OpenCV DLL
- Basler pylon 原生运行库、GigE 传输层和 `ProducerGEV.cti`
- MSVC 运行库

将完整的 `build/Release` 目录复制到测试电脑后，从该目录启动 `KY_DIMM.exe`：

```powershell
Copy-Item -Recurse build/Release <test-computer-path>/KY-DIMM
```

部署时请注意：

- 只复制 Release 目录，不要混用 Debug DLL。
- 目标电脑需要 Windows x64、匹配的 pylon 12.x 运行环境和相机驱动。
- 当前配置要求相机输出 `Mono8`，并按精确型号搜索 `acA1920-40gm`。
- 如果找不到相机，先在 Basler pylon Viewer 中确认设备、IP、子网、网卡和防火墙设置。

完整说明见 [`deploy/KY-DIMM-DEPLOYMENT.txt`](deploy/KY-DIMM-DEPLOYMENT.txt)。

## 项目结构

```text
.
├── cmake/                 # CMake 构建后的运行时部署脚本
├── deploy/                # 测试电脑部署说明
├── docs/                  # 设计文档与执行计划
├── src/                   # KY-DIMM C++/Qt 主程序
├── tests/                 # Python 源码级测试
├── AGENTS.md              # Codex/开发协作规则
├── CMakeLists.txt         # CMake 构建入口
└── KY-DIMM-项目介绍.md    # 项目背景和测量约定
```

## 开发工作流

项目协作约定记录在 [`AGENTS.md`](AGENTS.md) 中。简要规则如下：

1. `master` 是稳定主分支；开始工作前同步 `origin/master`。
2. 功能、修复和文档使用 `codex/<feature>` 分支。
3. 运行与改动相关的测试后，再提交变更。
4. 通过 Pull Request 合并到 `master`，默认使用 Squash and merge。
5. 不提交构建产物、IDE 配置、SDK 文件、运行时 DLL 或测试缓存。

## 许可证

当前仓库尚未声明开源许可证。除非后续加入明确的许可证文件，否则不应将本项目视为授予了公开再分发、修改或商业使用权利。

## 相关文档

- [项目介绍](KY-DIMM-项目介绍.md)
- [部署说明](deploy/KY-DIMM-DEPLOYMENT.txt)
- [迁移执行计划](docs/superpowers/plans/2026-08-11-ui-pylon-dimm-migration-execution-plan.md)
- [详细执行任务](docs/superpowers/plans/2026-08-12-ky-dimm-detailed-execution-tasks.md)
