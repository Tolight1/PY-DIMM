# KY-DIMM 项目介绍

更新时间：2026-08-12

## 1. 项目定位

`KY-DIMM` 是一个基于单台望远镜、单台相机的双星点大气参数测量软件。

与原 `DIMM` 项目的关系：

- 原 `DIMM`：基于两个望远镜、两个相机。
- 当前 `KY-DIMM`：基于一个望远镜、一个相机，但望远镜前端开了两个圆形窗口，其中一个窗口加棱镜，因此观测北极星时会在同一幅相机图像上形成两个星点。
- 计算流程总体保持与 `DIMM` 一致：定位两个星点 -> 计算两个质心 -> 形成差分量 -> 按 `DIMM` 原公式计算 `r0 / seeing / theta0 / tau0`。
- 物理基础与光路结构与原项目不同，但从“得到双星点之后”的计算公式要求保持与 `DIMM` 一致。

## 2. 当前硬件与默认参数

当前默认硬件假设如下：

- 项目名称：`KY-DIMM`
- 相机：`Basler acA1920-40gm`
- SDK：`E:\Softwoare\Basler pylon\Development`
- 采集默认像素格式：`Mono8`
- 全画幅默认分辨率：`1920 × 1200`
- 像元尺寸：`5.86 µm`
- 望远镜主镜口径：`254 mm`
- 子孔径直径：`60 mm`
- 两个圆形窗口中心距离：`150 mm`
- 焦距：`2500 mm`
- 波长：`550 nm`

这些参数都应该视为“可配置”，不能硬编码成不可修改常量；当前项目中已经通过设置窗口承载这些关键超参数。

## 3. 主要功能目标

当前软件的目标是：

- 连接 Basler 相机
- 采集北极星图像
- 在全画幅上找到两个星点
- 建立 `StarA / StarB` 两个软件 ROI
- 在 ROI 内用固定算法计算质心
- 形成差分抖动样本
- 用与 `DIMM` 一致的公式计算四个参数：
  - `r0`
  - `seeing`
  - `theta0`
  - `tau0`
- 在主界面实时显示全画幅、两个 ROI、四个参数、状态信息
- 以 CSV / JSON 形式保存结果和运行元数据

## 4. 质心与测量算法要求

当前实现采用的质心路径是：

- 原生 `cv::THRESH_OTSU`，并取 `max(Otsu, 均值 + 4σ, 均值 + 0.20 × (峰值 - 均值))`
- 可配置连通域，默认 `8 连通`
- 连通域面积默认 `9–1000 px²`
- 默认半径 `3 px` 的小核（即 `7×7`）强度加权质心

这条路径对应代码：

- [CentroidEngine.h](/E:/Softwoare/visual%20studio/project/UI/UI_pylon/src/CentroidEngine.h)
- [CentroidEngine.cpp](/E:/Softwoare/visual%20studio/project/UI/UI_pylon/src/CentroidEngine.cpp)

注意：

- 不迁移旧 `DIMM` 中暗场模板 / 热像素模板相关逻辑。
- 当前版本不保存原始全画幅图像和原始 ROI 图像。
- `r0` 的计算窗口不是固定 `60 s`，而是“按帧数滚动窗口”：
  - 例如默认 `1000` 帧
  - 当有效样本积累满 `1000` 后开始显示四个参数
  - 之后按当前设置的测量帧率每秒更新一次

## 5. 线程与模块划分

当前项目按“UI 线程”和“采集/处理线程”分离：

- `MainWindow`
  - 主界面、按钮、状态栏、预览刷新
  - 文件：
    - [MainWindow.h](/E:/Softwoare/visual%20studio/project/UI/UI_pylon/src/MainWindow.h)
    - [MainWindow.cpp](/E:/Softwoare/visual%20studio/project/UI/UI_pylon/src/MainWindow.cpp)
   - [MainWindow.ui](/E:/Softwoare/visual%20studio/project/UI/UI_pylon/src/MainWindow.ui)

- `CameraWorker`
  - 相机连接、断开、开始采集、停止采集
  - 管理 Basler pylon 相机对象
  - 文件：
    - [CameraWorker.h](/E:/Softwoare/visual%20studio/project/UI/UI_pylon/src/CameraWorker.h)
    - [CameraWorker.cpp](/E:/Softwoare/visual%20studio/project/UI/UI_pylon/src/CameraWorker.cpp)

- `PylonCamera`
  - 唯一允许直接接触 pylon SDK 的模块
  - 负责相机节点配置、曝光、触发、AOI、抓帧
  - 文件：
    - [PylonCamera.h](/E:/Softwoare/visual%20studio/project/UI/UI_pylon/src/PylonCamera.h)
    - [PylonCamera.cpp](/E:/Softwoare/visual%20studio/project/UI/UI_pylon/src/PylonCamera.cpp)

- `MeasurementWorker`
  - 从采集队列取帧
  - 执行双星跟踪、ROI 更新、差分样本形成、四参数计算
  - 文件：
    - [MeasurementWorker.h](/E:/Softwoare/visual%20studio/project/UI/UI_pylon/src/MeasurementWorker.h)
    - [MeasurementWorker.cpp](/E:/Softwoare/visual%20studio/project/UI/UI_pylon/src/MeasurementWorker.cpp)

- `TwoStarTracker`
  - 负责全画幅定位、StarA/StarB 身份稳定、软件 ROI 跟踪、硬件 AOI 请求
  - 文件：
    - [TwoStarTracker.h](/E:/Softwoare/visual%20studio/project/UI/UI_pylon/src/TwoStarTracker.h)
    - [TwoStarTracker.cpp](/E:/Softwoare/visual%20studio/project/UI/UI_pylon/src/TwoStarTracker.cpp)

- `AtmosphereCalculator`
  - 负责差分方差、`r0 / seeing / theta0 / tau0` 计算
  - 文件：
    - [AtmosphereCalculator.h](/E:/Softwoare/visual%20studio/project/UI/UI_pylon/src/AtmosphereCalculator.h)
    - [AtmosphereCalculator.cpp](/E:/Softwoare/visual%20studio/project/UI/UI_pylon/src/AtmosphereCalculator.cpp)

- `SettingsDialog`
  - 设置窗口
  - 当前分为：
    - 物理/光学参数
    - 采集参数
    - 图像处理参数
    - 触发设置
    - 数据存储
  - 文件：
    - [SettingsDialog.h](/E:/Softwoare/visual%20studio/project/UI/UI_pylon/src/SettingsDialog.h)
    - [SettingsDialog.cpp](/E:/Softwoare/visual%20studio/project/UI/UI_pylon/src/SettingsDialog.cpp)
    - [SettingsDialog.ui](/E:/Softwoare/visual%20studio/project/UI/UI_pylon/src/SettingsDialog.ui)

- `ResultWriter`
  - 写 `CSV / JSON`
  - 文件：
    - [ResultWriter.h](/E:/Softwoare/visual%20studio/project/UI/UI_pylon/src/ResultWriter.h)
    - [ResultWriter.cpp](/E:/Softwoare/visual%20studio/project/UI/UI_pylon/src/ResultWriter.cpp)

- `FrameQueue / DisplayMailbox`
  - 前者用于采集线程到测量线程的数据传递
  - 后者用于显示快照的 latest-only 邮箱
  - 文件：
    - [FrameQueue.h](/E:/Softwoare/visual%20studio/project/UI/UI_pylon/src/FrameQueue.h)
    - [FrameQueue.cpp](/E:/Softwoare/visual%20studio/project/UI/UI_pylon/src/FrameQueue.cpp)
    - [DisplayMailbox.h](/E:/Softwoare/visual%20studio/project/UI/UI_pylon/src/DisplayMailbox.h)
    - [DisplayMailbox.cpp](/E:/Softwoare/visual%20studio/project/UI/UI_pylon/src/DisplayMailbox.cpp)

## 6. 当前 UI 设计与交互约束

当前主界面核心交互：

- 单独的“连接相机 / 断开相机”按钮
- “开始采集 / 停止采集”按钮
- 采集中仍允许打开“设置”
- 全画幅预览与 ROI 预览分开刷新
- 右侧显示四个大气参数、有效样本进度、ROI 状态、相机状态

当前设置窗口要求：

- 图像处理参数不能是一整列冗余堆叠，应按逻辑分组
- 关键数值参数支持鼠标滚轮调节
- 软件触发、硬件触发、连续采集、部分扫描相关项要可配置

## 7. 当前已落实的重要行为

截至 2026-08-12，已经明确实现/修复的行为：

- `开始采集` 不再自动连接相机，必须先连接再开始
- 设置窗口在采集过程中可打开
- 图像处理参数页已经改为分组布局
- 质心参数和 ROI 参数支持滚轮改值
- `MeasurementWorker` 会周期性向 UI 推状态，不再只在结果发布点更新
- 测量率低于要求值时，四参数会被视为无效，不继续沿用旧有效值冒充结果
- AOI 切换后会重新尝试维持连续模式的目标采样帧率

## 8. 当前仍需硬件验证的部分

这些点代码已经写了，但必须在真实相机和真实光路上验证：

- Basler `acA1920-40gm` 在当前配置下是否稳定输出 `Mono8`
- 连续采集模式下是否能稳定受控到 `>= 100 Hz`
- 曝光 `1 ms / 3 ms / 5 ms` 等场景下，实测率是否符合预期
- AOI 切换后相机是否仍保持目标采样率
- 全画幅双星点定位在真实北极星图像上是否稳定
- ROI 跟踪与重定位阈值是否合适
- 丢星后回全画幅重定位是否稳定
- 四参数在有效样本达到窗口要求后是否按预期开始更新

## 9. 构建与部署

构建系统：

- `CMake + Qt6 + OpenCV + Basler pylon`
- 主工程文件：[CMakeLists.txt](/E:/Softwoare/visual%20studio/project/UI/UI_pylon/CMakeLists.txt)

部署目标：

- 希望每次构建后，直接复制 `build/Release` 到另一台装有相机 SDK 的电脑测试

相关文件：

- 部署脚本：[KY_DIMMDeploy.cmake](/E:/Softwoare/visual%20studio/project/UI/UI_pylon/cmake/KY_DIMMDeploy.cmake)
- 部署说明：[KY-DIMM-DEPLOYMENT.txt](/E:/Softwoare/visual%20studio/project/UI/UI_pylon/deploy/KY-DIMM-DEPLOYMENT.txt)

## 10. 测试状态

当前存在一组源码级静态测试，用于保护关键契约：

- [tests/test_compile_contracts.py](/E:/Softwoare/visual%20studio/project/UI/UI_pylon/tests/test_compile_contracts.py)
- [tests/test_physics_calculator.py](/E:/Softwoare/visual%20studio/project/UI/UI_pylon/tests/test_physics_calculator.py)
- [tests/test_roi_state_machine.py](/E:/Softwoare/visual%20studio/project/UI/UI_pylon/tests/test_roi_state_machine.py)
- [tests/test_plan_static.py](/E:/Softwoare/visual%20studio/project/UI/UI_pylon/tests/test_plan_static.py)

最近一次静态测试结果：

- `23 passed`

注意：

- 这是源码级/逻辑级测试，不等于真实硬件验证通过。
- 当前文档作者没有替你执行最终构建与现场联机测试。

## 11. 历史文档

如果新聊天框需要更详细的迁移背景或执行计划，可以继续读：

- [2026-08-11-ui-pylon-dimm-migration-execution-plan.md](/E:/Softwoare/visual%20studio/project/UI/UI_pylon/docs/superpowers/plans/2026-08-11-ui-pylon-dimm-migration-execution-plan.md)
- [2026-08-12-ky-dimm-detailed-execution-tasks.md](/E:/Softwoare/visual%20studio/project/UI/UI_pylon/docs/superpowers/plans/2026-08-12-ky-dimm-detailed-execution-tasks.md)

## 12. 给新聊天框的建议开场

如果在 Codex 中开新聊天框，建议直接说明：

1. 当前项目是 `KY-DIMM`，目录是 `E:\Softwoare\visual studio\project\UI\UI_pylon`
2. 先读本文件 `KY-DIMM-项目介绍.md`
3. 如果需要背景，再读 `docs/superpowers/plans` 下两份计划文档
4. 当前优先级通常应是：
   - 真实硬件联机问题
   - ROI 跟踪与双星定位稳定性
   - 100 Hz 硬门限下的测量有效性
   - UI 文案/布局微调

## 13. 特别提醒

- `src/UI_New.*` 是历史遗留文件，不是当前主程序入口。
- 当前真正主程序入口是：
  - [main.cpp](/E:/Softwoare/visual%20studio/project/UI/UI_pylon/src/main.cpp)
   - [MainWindow.cpp](/E:/Softwoare/visual%20studio/project/UI/UI_pylon/src/MainWindow.cpp)
- 新任务如果修改“开始采集”“连接相机”“参数刷新”“ROI 更新”等行为，优先看：
  - `MainWindow`
  - `CameraWorker`
  - `MeasurementWorker`
  - `TwoStarTracker`
  - `AtmosphereCalculator`
