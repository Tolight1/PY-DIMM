# KY-DIMM 单相机双星点测量项目迁移实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:subagent-driven-development` or `superpowers:executing-plans` to implement this plan task-by-task. 本计划中的步骤使用复选框跟踪。

> 给执行 agent 的任务文件。执行前必须先通读全文，并严格按任务顺序完成。  
> 本文件只描述实现任务，不要求本轮构建；用户明确要求由用户本人负责后续构建。

**目标：** 在现有 UI_pylon 工程中，基于 Basler pylon SDK 实现一台相机、一个望远镜、两个星点的最小可用测量闭环。系统需要能够在全画幅中找到北极星形成的两个星点，生成两个独立软件 ROI，跟踪随地球自转移动的星点，计算两个质心的差分位移、方差和四个大气参数，并在界面中显示和写入结果。

**方案：** 采用方案二：保留 UI_pylon 的现有工程骨架，迁移 UI_2 中已经验证过的纯算法和配置思想，但不整体复制 UI_2 的双相机业务链路。新项目使用单相机采集线程、单向有界帧队列、独立图像处理线程、独立结果写入线程；相机触发方式、空间 ROI 方式和显示频率彼此独立。

**技术栈：** C++、Qt 6.11.0 MSVC 2022、OpenCV 4.12.0、Basler pylon SDK 12.2.1、现有 UI_pylon 的 Qt Widgets 界面。

**Goal:** 在单 Basler 相机、单望远镜双星点光路下，完成从 1920 x 1200 初始定位、双软件 ROI、Mono8 质心、100 Hz 以上测量到四参数显示/存储的 KY-DIMM 闭环。

**Architecture:** GUI 线程只负责显示和控制；CameraWorker 独占 pylon 相机；MeasurementWorker 独立执行全画幅定位、硬件 AOI 后的双 ROI 质心和 DIMM 公式；ResultWriter 独立写入 CSV/JSON。硬件 AOI 是包住两个软件 ROI 的单个连续矩形，显示频率与测量频率完全隔离。

---

## 0. 执行边界和硬性规则

### 0.1 本轮必须完成

- 将 UI_pylon 的 CMakeLists.txt 改为 Qt6 + MSVC + OpenCV + pylon 的构建配置。
- 建立清晰的目录和模块边界。
- 实现单 Basler 相机的连接、参数配置、采集启动、停止和断开生命周期。
- 支持连续采集、软件触发、硬件触发三种模式。
- 支持初始 1920 x 1200 全画幅定位；支持在处理线程中建立两个独立软件 ROI。
- 支持一个包住两个软件 ROI 的连续外层硬件 AOI。硬件 AOI 不是两个硬件窗口，而是相机传感器上的一个连续大矩形；StarA/StarB 仍由软件在该大矩形内分别裁剪。
- 迁移参考文件中的 Otsu 连通域 + 小核质心方法；不再采用 IntensityCog/GaussianFit 作为新项目质心方法，也不迁移暗场/热像素链路。
- 实现双星点识别、StarA/StarB 稳定编号、质心、差分位移、方差和四个输出参数。
- 按 UI_2 的 ROI 更新机制实现动态 ROI：
  - 星点靠近 ROI 边缘时触发重定位判断；
  - 连续多帧满足条件才更新；
  - 更新期间设置冷却时间；
  - 星点丢失时回到全画幅定位；
  - 全画幅重新定位成功后再同时重建两个 ROI。
- 设置窗口包含以下四个功能参数页面，页面标题必须完全一致：
  - 物理/光学参数
  - 采集参数
  - 图像处理参数
  - 触发设置
- 设置窗口另外增加“数据存储”页面，参考 DIMM 的存储路径、参数记录和详细记录分层，但删除双相机同步诊断和暗场/热像素相关字段。
- 分离三种频率，并把测量频率作为硬性质量门槛：
  - 全画幅预览频率，默认几 Hz；
  - ROI 预览频率，高于全画幅预览；
  - 测量采集/处理频率，默认 100 Hz，并在配置校验中禁止低于 100 Hz；实际有效测量频率低于 100 Hz 时不得发布有效四参数。
- 所有物理量放入设置界面并持久化，不得把相机像元、波长、口径、基线、焦距等散落在业务代码中。
- 增加静态单元测试或纯算法测试；测试运行只允许使用 Python/CTest 之外的静态方式，不得触发 CMake 配置或 C++ 构建。
- 纯算法测试可以在内存中构造小尺寸 cv::Mat 或 Python 数组验证 Otsu、连通域、小核和窗口规则，但这只是测试夹具，不得形成运行时模拟采集功能。
- 不在本轮运行 cmake、MSBuild、ninja、make、Visual Studio Build，也不要启动相机 GUI 或修改硬件。

### 0.2 本轮明确不做

- 不迁移任何模拟采集功能：不新增模拟相机、模拟帧生产器、图像回放采集模式、模拟触发模式、模拟时间戳或“无相机运行”开关；运行采集链路只接受真实 Basler pylon 相机。
- 不迁移 UI_2 的双相机同步、双相机队列、相机 1 / 相机 2 时间偏移和同步丢帧补偿。
- 不迁移 Galaxy SDK 的 CameraManager、GX_* 调用、EAF、环境传感器、串口主机通信、网络通信、自动调焦、全自动采集流程。
- 不把 UI_2 的 ImageProcessor.cpp 整个复制到新项目。
- 不在 pylon 回调中直接运行质心、OpenCV 大计算、QWidget 操作或文件 I/O。
- 不用无限增长的 QVector、QList 或 std::queue 缓存图像。
- 不把软件触发误认为连续采集。软件触发必须是 TriggerMode=On、TriggerSource=Software，并由软件明确发出每一次触发命令；连续采集是 TriggerMode=Off。
- 不把“部分扫描/ROI 采集”与“触发方式”绑定。触发方式描述何时取一帧；ROI/AOI 描述每一帧取多大空间区域。
- 不在本轮修改 UI_pylon/build 中已有内容。

---

## 1. 当前工程与参考工程

### 1.1 当前新工程

工作目录：

E:/Softwoare/visual studio/project/UI/UI_pylon

当前文件：

- CMakeLists.txt
- src/main.cpp
- src/UI_New.cpp
- src/UI_New.h
- src/UI_New.ui

当前骨架是空的 Qt Widgets 主窗口，现有 CMakeLists.txt 使用 Qt5 和 MinGW 配置，必须替换为 Qt6、MSVC、OpenCV 和 pylon 配置。

### 1.2 参考工程

参考目录：

E:/Softwoare/visual studio/project/UI/UI_2

可迁移内容：

- src/CentroidLogic.h
- src/ImageUtils.h
- src/ImageUtils.cpp
- src/FullFrameStarDetector.h
- src/FullFrameStarDetector.cpp
- src/AppConfig.h
- src/AppConfig.cpp
- src/AppConfigPersistence.h
- src/AppConfigPersistence.cpp
- src/SettingsDialog.h
- src/SettingsDialog.cpp
- src/SettingsDialog.ui
- src/ResultWriter.h
- src/ResultWriter.cpp
- src/DIMM.Results.cpp
- src/DIMM.LiveRoi.cpp 中的 ROI 重定位思路和条件判断

只能参考、不能直接复制业务结构的内容：

- src/ImageProcessor.h
- src/ImageProcessor.cpp
- src/CameraManager.h
- src/CameraManager.cpp
- src/CameraThread.h
- src/CameraThread.cpp
- 所有依赖双相机同步的代码
- 所有依赖 Galaxy SDK 的代码

### 1.3 pylon SDK

SDK 根目录：

E:/Softwoare/Basler pylon/Development

CMake 配置目录：

E:/Softwoare/Basler pylon/Development/CMake/pylon

SDK 版本：

- Major 12
- Minor 2
- Subminor 1

必须优先使用 pylon 提供的 CMake imported target pylon::pylon，不要自行拼接 .lib 路径。

必须参考的 pylon 官方样例：

- Development/Samples/C++/Grab/Grab_UsingGrabLoopThread
- Development/Samples/C++/GUI/GUI_QtMultiCam
- Development/Samples/C++/Configuration/PixelFormatAndAoiConfiguration.h
- Development/Samples/C++/Utility/Utility_ImageFormatConverter.cpp

---

## 2. 推荐的最终目录结构

执行 agent 按下面的目录建立文件；文件名可以保持一致，不要把所有类继续堆到 UI_New.cpp 中。

~~~text
UI_pylon/
├─ CMakeLists.txt
├─ src/
│  ├─ main.cpp
│  ├─ MainWindow.h
│  ├─ MainWindow.cpp
│  ├─ MainWindow.ui
│  ├─ UiTheme.h
│  ├─ UiTheme.cpp
│  ├─ AppConfig.h
│  ├─ AppConfig.cpp
│  ├─ AppConfigPersistence.h
│  ├─ AppConfigPersistence.cpp
│  ├─ SettingsDialog.h
│  ├─ SettingsDialog.cpp
│  ├─ SettingsDialog.ui
│  ├─ CameraTypes.h
│  ├─ PylonCamera.h
│  ├─ PylonCamera.cpp
│  ├─ CameraWorker.h
│  ├─ CameraWorker.cpp
│  ├─ FrameQueue.h
│  ├─ FrameQueue.cpp
│  ├─ DisplayMailbox.h
│  ├─ DisplayMailbox.cpp
│  ├─ ProcessingTypes.h
│  ├─ TwoStarTracker.h
│  ├─ TwoStarTracker.cpp
│  ├─ CentroidEngine.h
│  ├─ CentroidEngine.cpp
│  ├─ AtmosphereParameterCalculator.h
│  ├─ AtmosphereParameterCalculator.cpp
│  ├─ AtmosphereWindow.h
│  ├─ AtmosphereWindow.cpp
│  ├─ MeasurementWorker.h
│  ├─ MeasurementWorker.cpp
│  ├─ ResultWriter.h
│  ├─ ResultWriter.cpp
│  ├─ ImageDisplayAdapter.h
│  └─ ImageDisplayAdapter.cpp
└─ tests/
   ├─ test_plan_static.py
   ├─ test_roi_state_machine.py
   └─ test_physics_calculator.py
~~~

说明：

- UI_New.h、UI_New.cpp、UI_New.ui 可以迁移为 MainWindow 三个文件；如果保留 UI_New 文件名，则必须让 CMake 和 main.cpp 统一使用同一套名字，不能同时保留两个主窗口入口。
- PylonCamera 只负责 pylon 设备生命周期和帧接收，不向上层暴露 pylon 类型。
- CameraWorker 只负责相机线程内的采集循环、触发调度和帧投递。
- MeasurementWorker 只负责算法流水线，不直接操作相机。
- AtmosphereParameterCalculator 只负责物理公式，不负责图像查找和线程。
- SettingsDialog 只负责配置编辑和校验，不负责改变运行中的相机；Apply 后由 MainWindow 统一协调停止、应用、重启需要重启的功能。
- ResultWriter 是唯一允许写测量 CSV 的线程。
- ResultWriter 参考 DIMM 的主结果/详细结果分离，但新项目扩展为 atmosphere_summary.csv、centroid_details.csv、acquisition_diagnostics.csv 和 run_metadata.json；不保存双相机同步诊断。

---

## 3. 线程模型和生命周期要求

### 3.1 线程分工

必须实现下面的线程结构：

~~~text
GUI 线程
  ├─ MainWindow / SettingsDialog / 画面显示 / 状态文本
  ├─ 只接收复制后的显示帧和结果
  └─ 不调用 pylon 设备对象

相机线程
  ├─ 唯一拥有 PylonCamera 和 CInstantCamera
  ├─ 设备打开、节点配置、StartGrabbing、StopGrabbing、关闭
  ├─ 连续/软件/硬件触发调度
  └─ 将回调中的图像深拷贝为 CameraFrame 后投递

处理线程
  ├─ 唯一拥有 TwoStarTracker、CentroidEngine、采样统计状态
  ├─ 从有界队列取帧
  ├─ 全画幅定位、双 ROI 追踪、质心和物理量计算
  └─ 发出 ROI 叠加信息、结果和处理状态

结果线程
  ├─ 唯一拥有输出文件句柄
  ├─ 从有界结果队列取 MeasurementResult
  └─ 批量写 CSV 并 flush
~~~

### 3.2 所有权规则

- pylon 设备对象只能在相机线程创建、使用和销毁。
- pylon 的 image event handler 不得持有 UI 指针。
- pylon 回调中只允许：
  1. 检查 grab result；
  2. 读取宽、高、像素类型、帧号和时间戳；
  3. 把图像拷贝到自有内存；
  4. 向 CameraWorker 投递自有 CameraFrame。
- pylon grab result 的生命周期结束后，算法不能继续引用其 buffer。
- cv::Mat 不得只保存 pylon buffer 的裸指针；必须使用 clone() 或明确的自有连续缓冲区。
- QWidget、QImage、QPainter 只能在 GUI 线程使用。
- SettingsDialog 不得在按钮槽中直接调用相机 SDK。
- ResultWriter 不得从 GUI 或 pylon callback 写文件。
- 所有跨线程信号传递的结构必须是值语义、可复制或使用明确的共享所有权；不能把栈上对象地址发送到其他线程。

### 3.3 队列规则

实现 FrameQueue，至少包含：

- 固定容量，默认容量 8；
- push(frame)；
- tryPopLatest(frame)，处理线程忙时优先取最新帧；
- tryPopOldest(frame)，需要顺序统计时使用；
- clear()；
- close()；
- 当前 size；
- 丢帧计数；
- QMutex + QWaitCondition 或等效的线程安全实现。

默认策略：

- 显示帧：只保留最新一帧，不允许显示 backlog。
- ROI 追踪：取最新可用帧，避免延迟扩大。
- 测量统计：测量模式下如果需要每一帧都参与统计，则使用有界 FIFO；队列满时丢弃最旧帧，同时增加 droppedForProcessing 计数，并在结果状态中标记丢帧。
- 任何队列都不能无限增长。
- 停止时先 close，再唤醒所有等待者，再 join 线程，再清空剩余数据。

### 3.4 线程停止顺序

MainWindow::stopMeasurement() 必须按以下顺序执行：

1. 将运行状态设为 Stopping，禁止新的开始操作。
2. 停止软件触发定时器或触发循环。
3. 请求 CameraWorker 停止抓取。
4. CameraWorker 在相机线程调用 StopGrabbing 并退出采集循环。
5. 关闭 CameraFrame 队列并唤醒处理线程。
6. MeasurementWorker 处理完当前帧后退出，不再等待新帧。
7. 关闭结果队列并唤醒 ResultWriter。
8. ResultWriter flush、关闭文件并退出。
9. 等待三个线程结束。
10. 清空处理状态和显示状态，最后恢复 GUI 控件。
11. 发生异常时也必须走同一个停止路径，不能只 return。

禁止在工作线程中调用 terminate() 强制杀线程。必须使用 requestStop、原子停止标志、close 队列和 wait。

### 3.5 generation 防止旧帧污染

每次开始一次新的测量会生成递增的 runGeneration：

- CameraFrame 带 generation；
- MeasurementResult 带 generation；
- ROI 状态带 generation；
- 线程重启或重新定位后，旧 generation 的帧和结果直接丢弃；
- GUI 收到结果时只接受当前 generation；
- 设备重连后必须清空帧队列和结果队列。

### 3.6 UI 线程、相机线程和高频数据的隔离优化

这是本项目的硬性要求。必须把“相机能不能稳定取到 100 Hz”和“界面能不能顺畅显示”设计成两个独立问题。UI 线程不能承担高频采集的节拍，也不能因为界面刷新变慢而阻塞相机。

#### 3.6.1 四类线程边界

除 pylon 内部回调线程外，代码中明确区分以下三类自有线程：

- GUI 线程：运行 MainWindow、SettingsDialog、QTimer、QImage/QPixmap 显示和状态文字更新；不包含 pylon 头文件，不创建相机对象，不执行质心、阈值分割、方差或 CSV 写入。
- CameraWorker 线程：运行 PylonCamera、设备节点配置、StartGrabbing/StopGrabbing、软件触发定时器和采集统计；不访问 QWidget、QLabel、QImage、QPixmap，不执行 OpenCV 算法。
- MeasurementWorker 线程：从 FrameQueue 取 CameraFrame，执行全画幅定位、两个 ROI 质心、差分、方差和物理量计算；不直接调用 pylon，不更新 QWidget。
- pylon 内部 image event handler 线程：只做 grab result 检查、元数据读取和一次必要的自有内存拷贝，然后投递到 CameraWorker 管理的队列/回调；不得把 pylon buffer 指针传出回调。

PylonCamera 的头文件和 pylon 类型只能出现在 PylonCamera.cpp/PylonCamera.h、CameraWorker.cpp/CameraWorker.h 等相机模块中。MainWindow.cpp、SettingsDialog.cpp、ImageDisplayAdapter.cpp 不得 include pylon 头文件。

#### 3.6.2 禁止每帧向 GUI 排队大图像

不能对每一帧调用带 cv::Mat/QImage 的 Qt queued signal 直接发送给 GUI。100 Hz 采集时，如果 GUI 消费速度只有 3 Hz 或 20 Hz，事件队列会积压旧图像，造成延迟、内存增长和停止卡顿。

新增 DisplayMailbox.h/.cpp，提供线程安全的 latest-only 数据邮箱：

- publishLatestFullFrame(const DisplayFrame&)：只保留最新全画幅显示帧；
- publishLatestRoiPair(const RoiDisplayFrame&)：只保留最新 StarA/StarB ROI 显示帧；
- publishLatestOverlay(const RoiOverlay&)：只保留最新 ROI 框、质心和状态；
  - snapshotFullFrame(...), snapshotRoiPair(...), snapshotOverlay(...)：GUI 定时读取显示快照；这里的 snapshot 只表示线程安全的内存快照，不是把原始图像保存到磁盘；
- clear(generation)：开始/停止/重定位时清除旧内容；
- droppedDisplayFrames()：记录被新帧覆盖的显示帧数量。

邮箱内部使用 QMutex 或 QReadWriteLock 保护值语义对象；共享图像必须是不可变的自有数据。禁止返回指向邮箱内部可变 cv::Mat 的裸指针。显示帧被新帧覆盖是预期行为，不算测量丢帧；测量丢帧必须由 FrameQueue 单独统计。

CameraWorker 只把深拷贝后的最新图像发布到 DisplayMailbox，同时把用于测量的 CameraFrame 投递到有界 FrameQueue。MeasurementWorker 把 ROI 预览和 overlay 发布到 DisplayMailbox。GUI 通过 QTimer 取邮箱快照，不消费 CameraWorker 的高频图像信号。

#### 3.6.3 GUI 显示节拍

MainWindow 创建三个 GUI 线程 QTimer，定时器只负责取最新快照和更新控件：

- fullFrameDisplayTimer：使用 fullFramePreviewRateHz，默认 3 Hz；
- roiDisplayTimer：使用 roiPreviewRateHz，默认 20 Hz；
- statusDisplayTimer：默认 5 到 10 Hz，刷新实际采集率、队列深度、丢帧计数和状态文字。

定时器触发时如果邮箱中没有新 generation 或没有新 frameId，直接 return；不能重复转换同一帧。QImage/QPixmap 的显示缩放只在显示定时器中执行，并使用 KeepAspectRatio；不得在 100 Hz 采集回调中转换 QImage。

GUI 定时器的单次工作只包括：取快照、必要的显示缩放、设置 QLabel/QGraphicsView 内容、更新少量文字。不得在定时器中执行全画幅候选检测、Otsu/连通域/小核质心、统计计算、文件操作或等待工作线程。

显示频率变更只修改 GUI 定时器周期，不修改相机采集频率；测量频率变更通过 CameraWorker 的线程消息应用，不从 GUI 直接改相机节点。

#### 3.6.4 CameraWorker 的采集优化

- 使用 CameraWorker 所在线程唯一拥有 CInstantCamera；通过 QObject::moveToThread 后，在该线程连接启动、配置和停止槽。
- 相机采集循环不能使用固定 sleep 阻塞 GUI；软件触发使用属于 CameraWorker 线程的 QTimer 或事件循环。
- pylon 回调不等待处理线程、不等待 GUI、不写文件；FrameQueue 满时按明确策略丢弃并计数，不能无限阻塞相机。
- 测量模式优先使用能保持帧顺序的抓取策略；显示-only 或明确允许抽样的场景才使用 latest-only 策略。实际选择必须在代码注释中说明，不能为了让 UI 流畅而静默丢弃测量帧。
- 1920 x 1200 全画幅数据的深拷贝、像素格式转换和队列投递耗时必须单独统计；算法耗时不能放在回调中。
- 处理跟不上时，CameraWorker 仍保持设备线程可响应；由 FrameQueue 计数 droppedForProcessing，由状态栏显示，不允许 CameraWorker 反向调用 MeasurementWorker 的同步方法。
- 每秒发布一次 acquisitionRateHz、grabCallbackTimeUs、queueDepth、droppedFrames 等轻量统计，不要每帧刷新 GUI 文字。
- 触发模式、曝光、增益、像素格式和硬件 AOI 的修改都通过 queued slot 进入 CameraWorker；配置期间停止抓取，应用成功后再恢复。

#### 3.6.5 MeasurementWorker 的算法隔离

- MeasurementWorker 从 FrameQueue 取帧，不通过 GUI 信号接收图像。
- 每帧算法只在 MeasurementWorker 线程执行；处理结果可以按测量帧产生，但通过 ResultWriter 队列写文件，不直接写磁盘。
- 高速结果不要逐帧更新全部 GUI 控件。MeasurementWorker 可以逐帧维护统计，但只以 5 到 10 Hz 发布轻量状态，以配置的 ROI 预览频率发布 ROI overlay，以需要记录的频率投递 ResultWriter。
- 当 ROI 进入 StarLostRelocating 时，继续维护状态但停止把无效配对计入统计；GUI 显示状态，旧结果必须明确标记为 last-known。
- MeasurementWorker 与 CameraWorker 之间只通过 CameraFrame、控制消息、状态信号和有界队列通信；禁止互相持有 QObject 裸指针并同步调用对方长耗时方法。

#### 3.6.6 跨线程连接和停止要求

- 状态、错误、开始完成、停止完成等轻量信号使用 Qt::QueuedConnection；不要依赖 AutoConnection 在对象移动线程后产生歧义。
- 高速图像不使用逐帧 queued signal；如必须发送 wake-up，只发送无参数的 frameAvailable，并使用 pendingWakeup 标志合并重复唤醒。
- MainWindow::closeEvent 和 stopMeasurement 不得在 GUI 线程等待一个永远不会结束的同步调用。先发停止消息、停止 GUI 定时器，再按 3.4 的顺序关闭队列和等待线程；等待超时必须显示线程停止异常，不能调用 terminate()。
- 相机线程退出前必须停止软件触发定时器；处理线程退出前必须停止消费；ResultWriter 退出前必须 flush 和关闭文件。
- 开始新的 generation 时，先清空 FrameQueue、DisplayMailbox 和 ResultWriter 队列，防止旧图像出现在新一轮 UI 中。

#### 3.6.7 UI/相机线程静态验收

新增静态检查：

- MainWindow.cpp、SettingsDialog.cpp、ImageDisplayAdapter.cpp 不包含 pylon 头文件；
- PylonCamera.cpp、CameraWorker.cpp 不包含 QWidget/QLabel/QPixmap，不调用 UI 控件；
- 不存在把 cv::Mat 或 QImage 逐帧 queued signal 发送到 GUI 的实现；
- 存在 DisplayMailbox，且全画幅、ROI、overlay 都使用 latest-only 读取；
- fullFramePreviewRateHz、roiPreviewRateHz、measurementRateHz 分别控制不同路径；
- CameraWorker、MeasurementWorker 和 ResultWriter 的停止信号、队列 close 和线程 wait 路径完整；
- 不存在跨线程同步调用相机长耗时方法或 GUI 阻塞等待每一帧的代码。

这部分静态检查可以直接加入 tests/test_plan_static.py；不需要相机，也不需要构建 C++。

---

## 4. 任务一：先建立公共数据类型和接口

### 4.1 新增 CameraTypes.h

定义以下类型，不能把 pylon 的 CGrabResultPtr 暴露到公共头文件：

  - enum class PixelFormat
  - Mono8
  - Unknown
- enum class TriggerMode
  - Continuous
  - Software
  - Hardware
- enum class AcquisitionState
  - Disconnected
  - Connecting
  - Ready
  - Running
  - Stopping
  - Error
- struct CameraFrame
  - qint64 frameId
  - qint64 timestampTicks
  - qint64 hostTimestampNs
  - int width
  - int height
  - PixelFormat pixelFormat
  - quint64 generation
  - cv::Mat image
  - bool isFullFrame
  - QRect sourceRect
- struct CameraCapabilities
  - QString modelName
  - QString serialNumber
  - int sensorWidth
  - int sensorHeight
  - bool supportsSoftwareTrigger
  - bool supportsHardwareTrigger
  - bool supportsAoi
  - bool supportsMono8
  - double maxFrameRate
  - QStringList triggerLines
- struct CameraStatus
  - AcquisitionState state
  - QString message
  - qint64 receivedFrames
  - qint64 droppedFrames
  - double measuredRateHz

CameraFrame 的 cv::Mat 必须是自有数据；提供 clone 或深拷贝构造方式。不要让默认浅拷贝导致跨线程共享可变缓冲区。

### 4.2 新增 ProcessingTypes.h

定义：

- enum class RoiTrackingState
  - Uninitialized
  - FullFrameLocating
  - RoiTracking
  - RecenterPending
  - HardwareAoiUpdating
  - StarLostRelocating
  - Error
- struct RoiRect
  - int x
  - int y
  - int width
  - int height
  - bool isValid()
  - QRect toQRect()
  - QRect clampTo(int imageWidth, int imageHeight)
- enum class StarId
  - StarA
  - StarB
- struct CentroidMeasurement
  - StarId id
  - bool valid
  - double x
  - double y
  - double peak
  - double integratedIntensity
  - double residual
  - double otsuThreshold
  - int selectedComponentArea
  - int smallKernelRadiusPx
  - quint64 usedPixelCount
  - RoiRect roi
  - QString failureReason
- struct TwoStarMeasurement
  - quint64 generation
  - qint64 frameId
  - qint64 timestampTicks
  - bool validPair
  - CentroidMeasurement starA
  - CentroidMeasurement starB
  - double dx
  - double dy
  - double longitudinalDisplacement
  - double transverseDisplacement
  - double longitudinalVariancePx2
  - double transverseVariancePx2
  - QString state
- struct AtmosphericResult
  - bool valid
  - QString invalidReason
  - double r0M
  - double seeingArcsec
  - double coherenceAngleArcsec
  - double coherenceTimeMs
  - double r0LongitudinalM
  - double r0TransverseM
  - double seeingLongitudinalArcsec
  - double seeingTransverseArcsec
  - quint64 windowFrameCount
  - quint64 validWindowSampleCount
  - quint64 updateFrameIndex
- struct MeasurementResult
  - TwoStarMeasurement sample
  - AtmosphericResult atmosphere
  - qint64 sampleIndex
  - qint64 validSampleCount
  - qint64 droppedFrameCount
  - double actualAcquisitionRateHz
  - double elapsedSeconds
  - quint64 generation
- struct RoiOverlay
  - RoiTrackingState state
  - RoiRect fullFrameOuterAoi
  - RoiRect starARoi
  - RoiRect starBRoi
  - CentroidMeasurement starA
  - CentroidMeasurement starB
  - QString message
  - quint64 generation

### 4.3 新增公共接口

实现以下接口，具体函数签名可按 Qt 风格调整，但职责必须保持：

- PylonCamera
  - open()
  - close()
  - capabilities()
  - configure(const CameraConfig&)
  - start()
  - stop()
  - executeSoftwareTrigger()
  - applyHardwareAoi(const RoiRect&)
  - resetToFullFrame()
  - setFrameCallback(...)
- CameraWorker
  - startWorker()
  - requestStop()
  - configure(...)
  - setGeneration(...)
  - signals: frameReady, statusChanged, errorOccurred, statisticsChanged
- TwoStarTracker
  - reset(generation)
  - locateOnFullFrame(const cv::Mat&, ...)
  - processRoiFrame(const cv::Mat&, ...)
  - shouldRelocalize()
  - currentOverlay()
  - state()
- CentroidEngine
  - compute(const cv::Mat&, const RoiRect&, const ProcessingConfig&)
- AtmosphereParameterCalculator
  - calculate(const TwoStarMeasurement&, const OpticalConfig&, const SamplingConfig&)
- MeasurementWorker
  - startRun()
  - requestStop()
  - setGeneration()
  - signals: resultReady, overlayReady, processingStatusChanged
- ResultWriter
  - openNewRun(...)
  - enqueue(const MeasurementResult&)
  - requestStop()
  - close()

公共接口中不能出现 GX_、CInstantCamera、CGrabResultPtr 或其他 pylon/Galaxy 类型。

---

## 5. 任务二：修改 CMakeLists.txt

必须直接修改：

E:/Softwoare/visual studio/project/UI/UI_pylon/CMakeLists.txt

不得保留旧的 Qt5、MinGW、Galaxy SDK 配置。目标配置如下，执行 agent 可以根据本机 Qt/OpenCV 的精确目录微调，但不能换回 Qt5。

~~~cmake
cmake_minimum_required(VERSION 3.21)

project(KY_DIMM LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_PREFIX_PATH "E:/Softwoare/Qtool/qt/6.11.0/msvc2022_64")
set(OpenCV_DIR "E:/Softwoare/OpenCV/opencv4120/build")

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)
find_package(OpenCV REQUIRED COMPONENTS core imgproc imgcodecs)
find_package(pylon 12.2.1 REQUIRED)

set(UI_PYLON_SOURCES
    src/main.cpp
    src/MainWindow.h
    src/MainWindow.cpp
    src/MainWindow.ui
    src/UiTheme.h
    src/UiTheme.cpp
    src/AppConfig.h
    src/AppConfig.cpp
    src/AppConfigPersistence.h
    src/AppConfigPersistence.cpp
    src/SettingsDialog.h
    src/SettingsDialog.cpp
    src/SettingsDialog.ui
    src/CameraTypes.h
    src/PylonCamera.h
    src/PylonCamera.cpp
    src/CameraWorker.h
    src/CameraWorker.cpp
    src/FrameQueue.h
    src/FrameQueue.cpp
    src/DisplayMailbox.h
    src/DisplayMailbox.cpp
    src/ProcessingTypes.h
    src/TwoStarTracker.h
    src/TwoStarTracker.cpp
    src/CentroidEngine.h
    src/CentroidEngine.cpp
    src/AtmosphereParameterCalculator.h
    src/AtmosphereParameterCalculator.cpp
    src/AtmosphereWindow.h
    src/AtmosphereWindow.cpp
    src/MeasurementWorker.h
    src/MeasurementWorker.cpp
    src/ResultWriter.h
    src/ResultWriter.cpp
    src/ImageDisplayAdapter.h
    src/ImageDisplayAdapter.cpp
)

add_executable(${PROJECT_NAME} WIN32 ${UI_PYLON_SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE
    ${OpenCV_INCLUDE_DIRS}
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt6::Widgets
    ${OpenCV_LIBS}
    pylon::pylon
)

if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /utf-8 /W4)
endif()

if(WIN32)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "E:/Softwoare/Basler pylon/Runtime/x64"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
        VERBATIM
    )
endif()
~~~

执行要求：

1. 用 pylon::pylon，不手工写 pylon.lib、PylonBase_v12.lib 等库名。
2. 使用 Qt6::Widgets，不使用 Qt5::Widgets。
3. 使用 C++17；pylon 12 和 Qt6 代码统一按 C++17 写。
4. OpenCV 只声明当前模块实际需要的组件；如果本机 OpenCV package config 不接受 COMPONENTS，则改为 find_package(OpenCV REQUIRED)，不要删除 OpenCV 链接。
5. CMake 源文件列表必须包含所有新增 .h、.cpp、.ui，避免只依靠 aux_source_directory 隐式发现。
6. UI_New 与 MainWindow 只能选一套，禁止出现两个 main window 实现。
7. POST_BUILD 运行库复制只写入构建输出目录，不修改 SDK Runtime 原目录。
8. 修改完 CMakeLists.txt 后，只做文本检查，不执行 CMake 配置或构建。

---

## 6. 任务三：实现 pylon 相机层

### 6.1 PylonCamera.cpp 初始化和清理

按官方 Grab_UsingGrabLoopThread 的生命周期实现：

- 程序启动后或 CameraWorker 线程启动时调用 PylonInitialize。
- 相机线程内创建 CTlFactory、设备对象和 CInstantCamera。
- open 时枚举设备：
  - 设备数为 0 时发出明确错误；
  - 默认打开第一个设备；
  - 同时读取型号、序列号和设备用户 ID；
  - 后续可由配置增加按序列号选择。
- 构造和析构必须成对调用。
- close 时先停止抓取，再清理 event handler，再关闭相机，再释放设备。
- PylonTerminate 必须在所有 pylon 对象销毁后调用。
- 异常必须转成 QString 错误信号，不能让异常穿过 Qt 线程边界。

### 6.2 图像事件处理

实现专用 ImageEventHandler 或等效回调：

- 回调线程不是 GUI 线程，也不保证是 CameraWorker 线程。
- 回调不做 OpenCV 算法。
- 回调只读取：
  - Width；
  - Height；
  - PixelType；
  - BlockID 或帧号；
  - Timestamp；
  - grab 状态。
- 将 Mono8 图像拷贝到自有连续内存；当前项目第一版不实现 Mono12、Mono12Packed 或 Mono16 数据路径。
- 如果相机实际输出不是 Mono8，直接停止启动流程并报告像素格式不符合要求；不通过隐式转换掩盖配置错误。
- 对未知像素格式直接丢帧并报告错误，不要把字节数猜成 8 bit。
- 对图像 origin、stride、ROI offset 做明确记录。
- 回调中不得使用 QImage、QPainter、QWidget、QSettings、QFile 或 cv::findContours。
- 回调只通过线程安全的回调转发或 bounded queue 投递 CameraFrame。

### 6.3 相机参数配置

在 PylonCamera::configure 中按能力探测后配置：

- PixelFormat：当前型号为 Basler acA1920-40gm。pylon Viewer 实机截图显示当前生效格式为 `Mono 8`，`Pixel Size = 8 Bits/Pixel`；本项目第一版固定使用 Mono8，设置界面显示为只读的“Mono8”，不提供 Mono12/Mono12Packed/Mono16 选项。
- 连接后仍必须读取相机节点 `PixelFormat` 和抓取结果 `GetPixelType()`，并在 UI/metadata 中记录实际格式；截图是当前设备配置证据，不能替代运行时读取。
- “采集参数”只提供 Mono8 只读项；如果相机节点无法设置为 Mono8，开始采集前必须报错。
- 相机链路约束：该型号官方标注默认分辨率为 1920 x 1200、默认设置帧率约 42 fps；因此不能把 1920 x 1200 全画幅持续采集当作 100 Hz 测量链路。启动后先用全画幅定位双星，再自动生成并应用一个包住两个软件 ROI 的连续外层硬件 AOI；硬件 AOI 应用后必须用真实帧时间戳测得实际频率。
- 100 Hz 是有效四参数的硬门槛，不是 UI 刷新目标：实际测量频率低于 100 Hz、硬件 AOI 不支持/应用失败或处理线程出现测量丢帧时，可以保留诊断数据和预览，但不得发布有效 r0、seeing、theta0、tau0；UI 和 metadata 必须标记 samplingRateInsufficient。
- ExposureTime：使用 pylon 的浮点节点并夹紧到 Min/Max。
- Gain：使用 pylon 的浮点节点并夹紧到 Min/Max。
- AcquisitionFrameRateEnable 和 AcquisitionFrameRate：节点存在且可写时才设置。
- TriggerMode：
  - Continuous：TriggerMode=Off；
  - Software：TriggerSelector=FrameStart，TriggerMode=On，TriggerSource=Software，TriggerActivation 按节点要求设置；
  - Hardware：TriggerSelector=FrameStart，TriggerMode=On，TriggerSource=配置的 LineN，TriggerActivation=RisingEdge 或用户配置的可用值。
- 在设置 TriggerMode 前，先停止抓取并检查节点可写。
- 对不存在或不可写的节点记录 warning，并根据配置校验决定是否拒绝启动。
- 软件触发和硬件 AOI 均需能力探测，不能假设所有型号都支持。
- 使用 CIntegerParameter、CFloatParameter、CEnumParameter 这类 pylon参数对象或等效的 GenApi 节点访问方式。
- Width、Height、OffsetX、OffsetY 的设置顺序要先恢复合法对齐，再设置尺寸，再设置偏移，并在每一步按 Min/Max/Inc 夹紧。
- 改变 AOI 时必须在停止抓取状态完成。

### 6.4 采集启动

- Continuous 模式：设置 TriggerMode=Off 后开始抓取。
- Software 模式：设置 TriggerMode=On + TriggerSource=Software 后开始抓取；由 CameraWorker 的触发定时器发出 ExecuteSoftwareTrigger。
- Hardware 模式：设置 TriggerMode=On + TriggerSource=LineN 后开始抓取，等待外部脉冲。
- 不要在同一个线程用固定 sleep 阻塞 GUI。
- 软件触发定时器必须属于 CameraWorker 线程，或由 CameraWorker 线程中的事件循环驱动。
- 软件触发周期由 measurementRateHz 计算，默认 100 Hz；必须监控实际送出的触发数和收到帧数。
- pylon 回调收到的 frameId 不保证连续，必须允许相机自身丢帧并计数。

---

## 7. 任务四：实现频率、触发和 ROI/AOI 的独立配置

### 7.1 配置语义

将以下三个字段完全分开：

- fullFramePreviewRateHz：全画幅预览频率，默认 3.0，允许范围 0.5 到 10。
- roiPreviewRateHz：ROI 预览频率，默认 20.0，允许范围 5 到 60。
- measurementRateHz：测量采集/处理频率，默认 100.0，允许范围从 100.0 开始；配置值是最低目标，实际频率仍必须现场测量。

说明：

- 预览限频不能降低测量采集频率。
- 处理频率是算法消费频率；有效统计路径不得为了显示而抽样或丢弃测量帧。若相机实际采集高于显示频率，显示只取 latest-only，统计仍逐帧消费。
- 全画幅预览几 Hz 只表示 GUI 刷新几 Hz，不表示测量只采集几 Hz。
- ROI 预览频率高于全画幅预览，但也不表示必须改变相机采集帧率。

### 7.2 触发设置

触发页面包含：

- triggerMode：连续 / 软件触发 / 硬件触发。
- hardwareLine：硬件触发线路，例如 Line1；只显示相机 capabilities 中存在的线路。
- hardwareActivation：上升沿 / 下降沿，如果节点支持。
- softwareTriggerRateHz：默认与 measurementRateHz 联动，但允许用户选择是否独立。
- triggerTimeoutMs：默认 1000。
- dropIncompleteFrames：默认 true。
- requireExternalTrigger：硬件模式下默认 true。

校验：

- 选择 Software 时，若相机 capabilities 不支持软件触发，禁止保存运行配置。
- 选择 Hardware 时，没有可用线路则禁止开始。
- 选择 Continuous 时，不发送 ExecuteSoftwareTrigger。
- 软件触发时，必须在每次触发前检查相机仍处于 grabbing 状态。
- 硬件触发模式超时只报告状态，不在相机线程中无限等待。

### 7.3 空间采集设置

采集页面或图像处理页面中加入：

- enableHardwareAoi：默认 true；这是满足测量 100 Hz 硬指标的默认路径，不等于 UI 必须高频刷新。
- hardwareAoiMarginPx：默认 64。
- forceFullFrameForRelocalization：默认 true。
- fullFrameRelocalizationIntervalMs：默认 1000；该参数控制必要的重定位节拍，不得在两个有效重定位之间周期性切回全画幅而破坏 100 Hz 测量链路。

硬件 AOI 与软件 ROI 的固定定义：

- 硬件 AOI 是相机真正读出/传输的一个连续矩形，用于减少读出面积和带宽；它可以包含两个星点，但不能表达两个互不相邻的圆形窗口。
- 软件 ROI 是 MeasurementWorker 在 CameraFrame 中裁剪出的两个独立 64 x 64（可配置）矩形，分别命名 StarA 和 StarB；它只影响算法输入和显示，不直接提高相机帧率。
- 进入高速测量后，硬件 AOI = 两个软件 ROI 的全局包围矩形 + hardwareAoiMarginPx，并按相机 AOI 增量对齐；软件 ROI 坐标保持在传感器全局坐标系，处理时减去 CameraFrame.sourceRect 的左上角。
- 如果硬件 AOI 未启用或应用失败，系统可以继续全画幅定位/诊断，但不能把该状态当作满足 100 Hz 的有效测量状态。
- maxHardwareAoiShiftPx：默认 64。
- allowSoftwareRoiOnly：默认 false；用户可以关闭硬件 AOI 做定位/诊断，但此时只允许显示和记录诊断，不允许发布有效四参数。

空间语义：

- 软件 ROI：相机仍输出外层图像，处理线程从 cv::Mat 中裁剪 StarA、StarB 两个矩形；可随每帧动态更新。
- 硬件 AOI：相机输出一个外层矩形，必须同时包围两个软件 ROI；因为一般相机硬件只有一个矩形 AOI，不实现两个不连续硬件窗口。
- 当硬件 AOI 开启时，处理线程计算两个星点 ROI 的包围盒，加 margin 后通过 signal 请求 CameraWorker 应用新的外层 AOI。
- CameraWorker 只能在安全边界应用 AOI：
  - 停止抓取；
  - 配置 Width/Height/Offset；
  - 更新 frame sourceRect；
  - 清空旧帧；
  - 重新启动抓取；
  - 递增 generation 或 AOI generation；
  - 通知处理线程重新确认两个软件 ROI。
- 硬件 AOI 只在初始定位成功、两颗星点稳定后切换一次；后续北极星移动优先通过软件 ROI 跟踪，不随每帧移动硬件 AOI。只有星点丢失并重新全画幅定位成功后，才再次更新外层 AOI。

---

## 8. 任务五：实现双星点检测和动态 ROI

### 8.1 初始全画幅定位

新增 TwoStarTracker.cpp，不要把逻辑直接写进 MainWindow。

全画幅定位步骤：

1. 将 Mono8 输入图像作为单通道算法数据；第一版不实现 Mono12/Mono12Packed/Mono16 解包或转换。
2. 计算背景估计，可使用中值、低百分位或 UI_2 ImageUtils 中已有方法。
3. 根据 thresholdMode 生成候选亮斑。
4. 过滤候选：
   - 面积下限和上限；
   - 峰值强度；
   - 圆度或宽高比；
   - 不能超出图像边界；
   - 不能把饱和异常区域混淆。
5. 对候选按强度、面积、形状评分排序。
6. 选择两个候选星点：
   - 两个候选必须都有效；
   - 两者中心距离在配置的 minStarSeparationPx 和 maxStarSeparationPx 内；
   - 不能把同一亮斑的两个局部峰当成两个星点。
7. 为两个星点生成初始 ROI。
8. 使用两点基线方向建立稳定编号：
   - 保存 baselineUnit = normalize(starB - starA)；
   - 后续全画幅重新定位时，按投影坐标和历史位置匹配 StarA、StarB；
   - 不能简单地每次按 x 坐标排序，因为相机旋转、星点斜向运动时会交换编号。
9. 若候选不足两个，状态为 StarLostRelocating，不生成部分结果。

### 8.2 星点 ROI 生成

默认每个星点 ROI 为固定宽高，默认 64 x 64 像素；实际值放在图像处理参数中。

ROI 生成规则：

- 以质心为中心；
- 尺寸按 roiWidthPx、roiHeightPx；
- clamp 到当前图像边界；
- 如果 clamp 后尺寸不足，判定 ROI 无效；
- 两个 ROI 不能重叠到无法区分；若重叠，调整为共同外层 AOI 但仍保持两个独立中心；
- ROI 不能以负数或超过当前 sourceRect 的坐标交给 pylon；
- CameraFrame.sourceRect 非全画幅时，星点坐标先转换到全局坐标，再转换到局部 ROI 坐标。

### 8.3 ROI 跟踪循环

每一帧处理流程：

1. 根据当前 CameraFrame.sourceRect 将当前星点全局坐标映射到输入图像坐标。
2. 分别裁剪 StarA 和 StarB 软件 ROI。
3. 对两个 ROI 各自调用 CentroidEngine。
4. 只要一个星点无效，本帧 validPair=false；不得用另一个星点伪造差分。
5. 两个星点都有效时，计算：
   - dx = xB - xA；
   - dy = yB - yA；
   - longitudinalDisplacement = dot((xB-xA, yB-yA), baselineUnit)；
   - transverseDisplacement = cross2d(baselineUnit, (xB-xA, yB-yA))。
6. 用当前质心更新下一帧软件 ROI 中心，但使用平滑或限幅，避免单帧异常把 ROI 跳走。
7. 判断是否需要重定位。

### 8.4 必须迁移的 UI_2 ROI 条件

从 UI_2/src/DIMM.LiveRoi.cpp 迁移思想，并改为单相机双 ROI。默认配置如下：

- edgeDistancePx = 16.0；
- consecutiveFrames = 5；
- cooldownMs = 3000；
- minimumShiftPx = 8.0；
- fullFrameRelocalizationIntervalMs = 1000；
- lostFramesBeforeRelocalization = 10；
- maxCentroidJumpPx = 32.0。

判断规则：

- 星点质心距当前 ROI 任意边小于 edgeDistancePx 时，记为 edge-near。
- 连续 consecutiveFrames 帧满足 edge-near，且中心移动超过 minimumShiftPx，才进入 RecenterPending。
- 如果在 cooldownMs 内刚完成过 ROI 更新，不再次更新。
- 若质心瞬间跳跃超过 maxCentroidJumpPx，当前帧无效；连续达到 lostFramesBeforeRelocalization 后进入 StarLostRelocating。
- 任意一颗星连续丢失达到阈值，都必须回到全画幅定位。
- 不能只根据单帧边缘情况更新 ROI。
- 重定位期间仍可保留最近一次 validPair 作为 UI 的 last-known 状态，但不能继续把它计入新样本。
- 全画幅重定位成功后，必须一次性提交 StarA 和 StarB 两个初始 ROI，不能只更新一颗星。
- 两星点配对失败时，不得把两个独立候选分别提交成结果。

### 8.5 状态机

实现明确状态转换：

~~~text
Uninitialized
  -> FullFrameLocating       启动或 reset

FullFrameLocating
  -> RoiTracking             找到并编号两个星点
  -> StarLostRelocating      定位失败且超过重试周期
  -> Error                   图像尺寸或配置不可恢复错误

RoiTracking
  -> RecenterPending         连续边缘条件满足
  -> StarLostRelocating      一颗或两颗星连续丢失
  -> Error                   输入格式不支持

RecenterPending
  -> RoiTracking             软件 ROI 更新成功
  -> HardwareAoiUpdating     硬件 AOI 开启且外层 AOI 需要移动
  -> StarLostRelocating      更新所需星点无效

HardwareAoiUpdating
  -> RoiTracking             相机 AOI 应用完成并重新确认星点
  -> FullFrameLocating       AOI 应用失败，强制全画幅
  -> Error                   相机不支持或配置不可恢复

StarLostRelocating
  -> FullFrameLocating       到达全画幅重定位周期
  -> RoiTracking             重定位成功后提交双 ROI
  -> Error                   连续失败超过用户配置的最大时长
~~~

每次状态变化必须发出 overlayReady 或 processingStatusChanged，GUI 可以看到正在全画幅重定位，而不是显示旧 ROI 但不说明状态。

---

## 9. 任务六：迁移质心算法并隔离物理公式

### 9.1 CentroidEngine

质心方法改为参考文件 E:/Softwoare/visual studio/project/UI/备份/src87m/ImageProcessor.h 和 ImageProcessor.cpp 中的 Otsu 连通域 + 小核质心方法。第一版固定使用该方法，不再提供 IntensityCog/GaussianFit 二选一，也不迁移暗场模板或热像素修正。

需要参考并抽取的纯算法依赖包括：

- StarSegmentation/StarSegmentationCore：对单通道 Mono8 图像收集有限像素值，以 4096 个 histogram bins 计算 Otsu 阈值，然后使用 image > otsuThreshold 生成前景 mask；
- ConnectedDomain：使用 4 连通域；
- RoiComponentSelection：按最小连通域面积过滤；只有一个有效组件时直接选择，多个有效组件时按照上一帧全局质心距离选择；没有上一帧质心时不能随机选择；
- CentroidLogic 的小核强度加权部分：以连通域质心四舍五入后的像素为中心，在默认 radiusPx=3 的方形小核内，对有限且大于 0 的原始像素值做强度加权平均；
- OpenCV connectedComponentsWithStats：获取 label、面积和局部质心。

只迁移上述算法思想和必要的纯函数。禁止迁移以下内容：

- HotPixelTemplate、HotPixelRoiCache、applyHotPixelCorrection；
- hot pixel mask、hot pixel excess、strongHotPixelExcessDn；
- 暗场图、热像素模板文件、热像素缓存；
- ImageProcessor.h/.cpp 的双相机配对、cameraIndex、同步偏移和 pending 双相机队列；
- 参考工程中与自动曝光、环境传感器、串口或自动采集有关的代码。

CentroidEngine::compute 必须：

- 输入 const cv::Mat& 和 RoiRect；
- 输出 CentroidMeasurement；
- 不修改输入图像；
- 按以下固定顺序处理：单通道检查 → Otsu 阈值 → 二值前景 mask → 4 连通域 → 最小面积筛选 → 上一帧全局质心选组件 → 半径 3 px 小核强度加权质心；
- 对空图、越界 ROI、尺寸不足、全零、无有效组件、NaN、Inf 返回 valid=false 和 failureReason；
- 第一版只支持 Mono8；收到其他 PixelType 必须报告错误并停止有效测量；
- 坐标返回全局图像坐标；
- 明确像素中心坐标约定，统一使用 x=0 到 width-1、y=0 到 height-1；
- 输出 otsuThreshold、selectedComponentArea、smallKernelRadiusPx、usedPixelCount 和 integratedIntensity，便于 UI 和 CSV 诊断；
- Otsu 分箱数、最小/最大连通域面积、最少有效像素数、小核半径和候选间距必须从 ProcessingConfig 读取；禁止在 CentroidEngine.cpp 中写不可修改的业务常量，只有算法结构常量 4-connectivity 可以固定；
- 不依赖 QSettings、QWidget 或 pylon；
- 能够被 Python 静态测试对应的公式/状态逻辑间接检查。

### 9.2 差分样本和统计

新增 AtmosphereWindow.h/.cpp，专门维护四参数的按帧计算窗口；不要再使用 DIMM 的固定 60 秒历史窗口。

AtmosphereWindow 至少包含：

- capacityFrames，默认 1000；
- 固定容量的环形缓冲区；
- 最新窗口内的 longitudinal/transverse 差分样本；
- validPair 样本数、无效帧数、丢帧数；
- frameCounterSinceLastUpdate；
- validSampleCount、firstFrameId、lastFrameId；
- 可用于计算均值和方差的 Welford 状态，或每次窗口更新时对 1000 个样本进行稳定重算。

窗口规则：

- 默认 r0WindowFrames=1000；
- 只有 validPair=true 的双星差分样本进入方差窗口；无效双星帧不伪造样本，但仍计入 received/invalid frame 统计；
- 窗口未积累满 r0WindowFrames 个有效双星样本之前，四个参数显示“等待窗口：x/r0WindowFrames”，不得显示 0 或旧结果冒充当前结果；默认显示为“x/1000”；
- 窗口满后采用滚动窗口：每新增一个有效样本，移除最旧样本，保持最多 r0WindowFrames 个有效样本；
- 新一轮 runGeneration、物理参数改变、ROI 重新建立或相机重新连接时清空窗口；
- 采集运行总帧数、有效样本数、无效帧数和丢帧数独立记录；采集总时长不再决定四参数的计算窗口。

四参数更新节拍：

- atmosphereUpdateIntervalSeconds 默认 1.0 秒，并作为高级参数允许用户修改，建议范围 0.1 到 10.0 秒；设置为 1.0 秒时满足当前项目“每秒更新一次”的默认要求；
- updateEveryFrames = max(1, round(configuredMeasurementRateHz * atmosphereUpdateIntervalSeconds))；
- 以 MeasurementWorker 实际处理的测量帧计数，不以 GUI 刷新帧计数；例如设置 100 Hz，则每处理 100 帧触发一次四参数更新；
- 第 r0WindowFrames 个有效双星样本到达后立即生成第一次有效结果；默认是第 1000 个样本；之后每 updateEveryFrames 个测量处理帧重新计算一次最近 r0WindowFrames 个有效样本；
- 如果更新时窗口内有效样本不足 r0WindowFrames，则保持无效状态并显示当前有效样本数，不使用旧窗口补齐；
- 更新时使用当前配置的物理参数和当前 generation，结果携带 windowFrameCount=r0WindowFrames、validWindowSampleCount、updateFrameIndex；
- 实际采样率低于配置值时，状态区显示实际频率；实际测量频率低于 100 Hz 时，结果必须标记 samplingRateInsufficient 并保持四参数无效，而不是只把它当作普通 warning。

MeasurementWorker 仍维护：

- validPairSamples；
- invalidPairSamples；
- droppedFrames；
- lastFrameId；
- lastTimestamp；
- runStartHostTime；
- AtmosphereWindow；
- longitudinal 和 transverse 两个独立统计量。

采集达到 sampleCount 或 durationSeconds 时可以结束一次运行，但这两个字段只控制运行生命周期，不得再作为四参数的 60 秒或其他时间窗口。若配置为手动停止，则四参数可以持续按帧滚动更新。

### 9.3 AtmosphereParameterCalculator

将参考 ImageProcessor.cpp 中 calculateAtmosphere 的公式集中迁移到纯类中。输入是 AtmosphereWindow 的最近 r0WindowFrames 帧窗口快照，默认 1000 帧，不接收固定秒数历史列表。输出的四个主参数固定为：r0、seeing、theta0/相干角、tau0/相干时间；纵向/横向方差和 r0 分量只作为诊断字段。

建议接口：

- AtmosphereResult calculate(const AtmosphereWindowSnapshot& window, const OpticalConfig& optical, double actualRateHz)；
- calculator 不维护历史窗口、不负责计时、不访问 QSettings；
- AtmosphereWindow 负责帧窗口和按 atmosphereUpdateIntervalSeconds 更新节拍，AtmosphereParameterCalculator 只负责单位转换、方差和物理公式。

OpticalConfig 至少包含：

- subApertureDiameterMm；
- mainTelescopeApertureMm；
- baselineSeparationMm；
- baselineAngleDeg；
- focalLengthMm；
- zenithAngleDeg；
- wavelengthNm；
- pixelSizeUm。

第一版默认值：

- subApertureDiameterMm = 60.0；
- mainTelescopeApertureMm = 254.0，按 LX200-ACF 10 英寸型号的有效口径设置；该值用于设备物理信息展示，不直接代入当前 DIMM 子孔径公式；
- baselineSeparationMm = 150.0，表示望远镜两个圆形窗口中心之间的物理距离，作为 DIMM 公式中的 B；
- baselineAngleDeg = 0.0；
- focalLengthMm = 2500.0；
- zenithAngleDeg = 0.0；
- wavelengthNm = 550.0；
- pixelSizeUm = 5.86。

说明：

- 当前望远镜按 LX200-ACF 10 英寸型号配置：主望远镜有效口径 254 mm、焦距 2500 mm、f/10；彩页中的子孔径是 6 cm，公式中默认使用 60 mm 的子孔径，不要误把 254 mm 主镜口径当作 D。
- 北极星的天顶角和两个圆形窗口的物理中心距 B 必须在设置中维护；默认 B=150.0 mm，不允许把相机上两个星点的像素间距当成 B。
- `focalLengthMm=2500.0`、`wavelengthNm=550.0` 是当前 LX200-ACF 10 英寸配置的初始默认值，仍必须可配置。
- Basler 官方 acA1920-40gm 页面确认该型号使用 Sony IMX249，像元尺寸为 5.86 x 5.86 µm；因此默认 `pixelSizeUm=5.86`，设置页仍保留可编辑字段以支持后续更换相机或标定修正。

三个容易混淆的“距离/尺寸”必须分开命名：

- `roiWidthPx/roiHeightPx`：相机像素坐标中的软件 ROI 尺寸，例如 64 x 64 就是 64 个像素宽、64 个像素高，只决定裁剪范围。
- `starSeparationPx`：StarA 与 StarB 两个相机星点质心的图像间距，只用于候选配对、跟踪和诊断，不直接代入 DIMM 光学公式。
- `baselineSeparationMm`：望远镜两个圆形入射窗口中心之间的物理中心距，单位毫米，默认 150.0 mm，直接作为 DIMM 模型中的基线 B；它不是软件 ROI 中心距，也不是两个星点在相机上的像素间距。

因此，当前“两个窗口中心距离”明确指望远镜入射端两个圆形窗口的物理中心距，默认 150.0 mm。棱镜只负责让同一颗北极星在相机上形成两个光斑，不作为四参数公式的输入，也不需要在软件中建立棱镜等效基线模型。

第一版公式迁移要求：

- pixelScaleRad = pixelSizeUm * 1e-6 / (focalLengthMm * 1e-3)；
- `dx = StarB.x - StarA.x`、`dy = StarB.y - StarA.y`，再按 `baselineAngleDeg` 旋转：
  - `longitudinal = dx*cos(angle) + dy*sin(angle)`；
  - `transverse = -dx*sin(angle) + dy*cos(angle)`。
- 对每个方向先求样本均值，再按 DIMM 当前实现使用 `sum((x-mean)^2) / N`，明确不改成 `N-1`。
- 将纵向和横向像素位移方差乘以 pixelScaleRad 的平方，转换为弧度方差；
- 使用 UI_2 中当前的纵向系数：
  2 * wavelength^2 * (0.179 * D^(-1/3) - 0.0968 * B^(-1/3))；
- 使用 UI_2 中当前的横向系数：
  2 * wavelength^2 * (0.179 * D^(-1/3) - 0.145 * B^(-1/3))；
- r0 = pow(coefficient / sigma2, 3.0 / 5.0)；
- `r0LineOfSight = 0.5 * (r0Longitudinal + r0Transverse)`；
- `r0Zenith = r0LineOfSight * cos(zenithAngle)^(-3/5)`，其中角度先转弧度；
- `seeing = 0.98 * wavelength / r0Zenith * 206265`，单位 arcsec；
- `theta0 = 0.64 * (4 / sigmaLongitudinalArcsec2^0.65) * cos(zenithAngle)^(8/5)`，保持 DIMM 当前代码的表达式和单位；
- `tau0` 对最近 3 s 差分序列逐方向计算归一化自相关，在最大 200 ms 延迟内寻找 `1/e` 穿越；第一个采样间隔就穿越时标记 `underResolved`，跨越两个采样点时按相邻相关值线性插值；纵向/横向都有效时取两者平均，任一方向欠分辨时按参考实现返回欠分辨状态；
- D、B、f、λ 在计算器内部统一转换为米；像元尺寸从 µm 转米；`sigmaLongitudinal2` 和 `sigmaTransverse2` 为弧度平方；
- 对非正 D、B、f、wavelength、sigma2 或非有限数，返回无效结果和明确原因；
- 角度换算、arcsec 换算、zenith 修正必须集中在本类；
- 禁止在 MainWindow、TwoStarTracker 或 MeasurementWorker 中重复写公式。

第一版必须在代码注释和结果元数据中说明：这是从 UI_2 迁移的 DIMM 物理模型；棱镜只负责让同一颗北极星在相机上形成两个光斑，不作为四参数公式的输入。后续若子孔径或窗口物理定义发生变化，替换点只允许位于 AtmosphereParameterCalculator 及其测试。

四参数发布规则：

- r0：使用最近窗口的纵向/横向 r0 分量，按参考实现形成视线方向平均值并做天顶角修正；
- seeing：使用参考实现的 r0 和波长换算为 arcsec；
- theta0：使用窗口内纵向方差和天顶角计算相干角；
- tau0：沿用 DIMM 的自相关穿越 `1/e` 方法，在四参数同一份 1000 个有效差分样本快照中取最近 `tau0HistoryWindowSeconds=3.0` 秒数据，最大搜索延迟 `tau0MaxLagMs=200.0`，最少样本数 `tau0MinSamples=30`；如果窗口长度或采样率不足以分辨 tau0，返回无效或 underResolved，并显示原因；
- 四个参数必须在同一个 windowUpdateFrameIndex 下生成，不能让 r0 使用新窗口而 tau0 使用旧窗口。

---

## 10. 任务七：配置模型、持久化和设置窗口

### 10.1 AppConfig 分组

新增或改造 AppConfig，至少包含：

- OpticalConfig physical;
- AcquisitionConfig acquisition;
- ProcessingConfig processing;
- TriggerConfig trigger;
- StorageConfig storage;
- UiConfig ui。

物理量不得隐藏在 UI 控件的默认文本中；UI 初值来自 AppConfig，保存后由 AppConfigPersistence 写入 QSettings。

### 10.1.1 关键超参数必须可配置

以下参数是当前算法和统计窗口的关键超参数，必须同时满足“AppConfig 有字段、SettingsDialog 有控件、AppConfigPersistence 有键、开始测量前有校验、结果 metadata 有快照”五项要求：

- otsuHistogramBins：Otsu 直方图分箱数，默认 4096，允许范围 256 到 16384；
- minComponentAreaPx：连通域最小面积，默认 3，必须为正整数；
- maxComponentAreaPx：连通域最大面积，默认 0 表示不限制，否则必须大于等于最小面积；
- minSignalPixels：小核最少有效像素数，默认 3；
- smallKernelRadiusPx：小核半径，默认 3，允许范围 1 到 20；
- roiWidthPx、roiHeightPx：单星点软件 ROI 尺寸，默认 64 x 64；
- minStarSeparationPx、maxStarSeparationPx：全画幅双星候选间距范围；
- maxCentroidJumpPx：相邻帧质心最大允许跳变，默认 32；
- edgeDistancePx、consecutiveFrames、cooldownMs、minimumShiftPx、lostFramesBeforeRelocalization：动态 ROI 重定位参数；
- r0WindowFrames：四参数滚动计算窗口，默认 1000 帧；
- atmosphereUpdateIntervalSeconds：四参数更新周期，默认 1.0 秒；
- fullFrameRelocalizationIntervalMs、hardwareAoiMarginPx：全画幅重定位和外层 AOI 参数。
- tau0HistoryWindowSeconds：tau0 自相关子窗口，默认 3.0 s；
- tau0MaxLagMs：tau0 最大搜索延迟，默认 200 ms；
- tau0MinSamples：tau0 最少样本数，默认 30；
- minimumValidMeasurementRateHz：有效四参数最低实测频率，固定为 100 Hz，不允许在设置页降到 100 Hz 以下。

以下属于算法结构约束，第一版固定以保证与参考方法一致，不作为普通运行参数切换：

- Otsu + 4 连通域 + 小核质心的主流程；
- ConnectedDomain::kConnectivity=4；
- validPair 才能进入方差窗口；
- 不启用暗场模板、热像素 mask 或热像素 excess。

如果后续要改变这些结构约束，必须作为新的算法版本或标定任务处理，并在结果 metadata 中记录算法版本，不能让用户在运行中随意切换后混合比较数据。

### 10.2 物理/光学参数页面

页面标题必须是：

物理/光学参数

字段：

- 子孔径/有效口径 D，单位 mm，默认 60.0；
- 主望远镜有效口径，单位 mm，默认 254.0；按 LX200-ACF 10 英寸型号配置，该字段用于设备物理信息展示，当前 DIMM 子孔径公式仍使用 D=60.0 mm；
- 两个窗口中心距 B，单位 mm，默认 150.0；直接作为 DIMM 公式中的 baselineSeparationMm；
- 基线方向角，单位 deg，默认 0.0；
- 焦距 f，单位 mm，默认 2500.0；
- 观测天顶角，单位 deg，默认 0.0；
- 波长，单位 nm，默认 550.0；
- 像元尺寸，单位 um，默认 5.86；
- 相机水平像素，默认 1920；
- 相机垂直像素，默认 1200；
- 物理量单位说明和“B 为两个圆形窗口的物理中心距，默认 150.0 mm”的提示。

校验：

- D、B、f、wavelength、pixelSize 必须为有限正数才能开始有效测量；
- zenithAngleDeg 必须在 0 到 90 之间；
- baselineAngleDeg 可在 -180 到 180 之间；
- 分辨率必须是正整数；
- 不满足条件时保存可以暂存，但“开始测量”必须被阻止并在状态栏提示缺失字段。

### 10.3 采集参数页面

页面标题必须是：

采集参数

字段：

- ExposureTime，单位 us，默认 1000；
- Gain，单位 dB，默认 0；
- 像素格式固定 Mono8；界面显示相机实际生效的 PixelFormat，若不是 Mono8 则禁止开始有效测量；
- 全画幅预览频率，单位 Hz，默认 3；
- ROI 预览频率，单位 Hz，默认 20；
- 测量采集/处理频率，单位 Hz，默认 100；
- 样本数，默认 2000；
- 持续时间，单位 s，默认 20；
- enableHardwareAoi，默认开启；
- hardwareAoiMarginPx，默认 64；
- fullFrameRelocalizationIntervalMs，默认 1000。

校验：

- measurementRateHz < 100 时明确拒绝；
- sampleCount < 2 时拒绝；
- durationSeconds <= 0 时拒绝；
- 曝光时间必须小于一个测量周期，若不满足给出警告或阻止启动；
- 预览频率必须小于或等于实际相机帧率，若未知只提示；
- ROI 预览频率必须高于或等于全画幅预览频率；
- 自动按相机能力夹紧时，界面必须显示实际生效值。

### 10.4 图像处理参数页面

页面标题必须是：

图像处理参数

字段：

- centroidMethod：固定为 Otsu 连通域 + 小核质心；第一版不提供 GaussianFit 或旧质心方法切换；
- otsuHistogramBins，默认 4096，可编辑，范围 256 到 16384；
- minComponentAreaPx，默认 3；
- maxComponentAreaPx；
- minSignalPixels，默认 3；
- smallKernelRadiusPx，默认 3，范围 1 到 20；
- roiWidthPx，默认 64；
- roiHeightPx，默认 64；
- minStarSeparationPx；
- maxStarSeparationPx；
- maxCentroidJumpPx，默认 32；
- enableSmoothing；
- smoothingAlpha；
- ROI 重置参数组：
  - edgeDistancePx = 16；
  - consecutiveFrames = 5；
- cooldownMs = 3000；
- minimumShiftPx = 8；
- lostFramesBeforeRelocalization = 10；
- 大气参数滚动窗口：
  - r0WindowFrames，默认 1000；
  - 四参数更新周期，默认 1 s，可编辑范围 0.1 到 10 s；
  - 按当前 measurementRateHz 自动显示每次更新对应的帧数，例如 100 Hz 时为每 100 帧更新；
- 是否显示检测阈值图、是否显示质心十字线、是否显示 ROI 边框。

本页参数校验：

- otsuHistogramBins 必须是 256 到 16384 的整数；
- minComponentAreaPx >= 1；maxComponentAreaPx=0 表示不限制，否则必须不小于 minComponentAreaPx；
- minSignalPixels >= 1；smallKernelRadiusPx 在 1 到 20；
- roiWidthPx、roiHeightPx 必须不小于 16，并且不能超过当前图像尺寸；
- minStarSeparationPx > 0 且小于 maxStarSeparationPx；
- r0WindowFrames >= 2，默认 1000；atmosphereUpdateIntervalSeconds 在 0.1 到 10.0；
- 修改这些参数后必须清空现有 AtmosphereWindow，不能用不同窗口长度的样本混合生成同一轮结果。

页面底部提示必须明确：

Otsu 阈值、4 连通域和小核质心用于当前相机的星点质心计算；不使用暗场模板或热像素修正。ROI 参数只控制运行中的软件 ROI 重定位；大气参数使用最近 r0WindowFrames 个有效双星样本滚动计算，窗口未满前不显示有效四参数。

### 10.5 触发设置页面

页面标题必须是：

触发设置

字段：

- triggerMode：连续采集 / 软件触发 / 硬件触发；
- hardwareLine；
- hardwareActivation；
- softwareTriggerRateHz；
- triggerTimeoutMs；
- dropIncompleteFrames；
- requireExternalTrigger。

页面中必须加入说明：

- 连续采集：TriggerMode=Off，相机持续输出；
- 软件触发：TriggerMode=On + TriggerSource=Software，由程序发送每一次触发；
- 硬件触发：TriggerMode=On + TriggerSource=LineN，等待外部脉冲；
- ROI/AOI 是空间范围设置，与触发模式相互独立。

### 10.6 QSettings 键和持久化

AppConfigPersistence 使用稳定分组：

~~~text
[physical]
subApertureDiameterMm
mainTelescopeApertureMm
baselineSeparationMm
baselineAngleDeg
focalLengthMm
zenithAngleDeg
wavelengthNm
pixelSizeUm
sensorWidthPx
sensorHeightPx

[acquisition]
exposureUs
gainDb
pixelFormat
fullFramePreviewRateHz
roiPreviewRateHz
measurementRateHz
sampleCount
durationSeconds
enableHardwareAoi
hardwareAoiMarginPx
fullFrameRelocalizationIntervalMs

[processing]
centroidMethod
otsuHistogramBins
minComponentAreaPx
maxComponentAreaPx
minSignalPixels
smallKernelRadiusPx
roiWidthPx
roiHeightPx
minStarSeparationPx
maxStarSeparationPx
maxCentroidJumpPx
edgeDistancePx
consecutiveFrames
cooldownMs
minimumShiftPx
lostFramesBeforeRelocalization
r0WindowFrames
atmosphereUpdateIntervalSeconds

[trigger]
triggerMode
hardwareLine
hardwareActivation
softwareTriggerRateHz
triggerTimeoutMs
dropIncompleteFrames
requireExternalTrigger

[storage]
storagePath
filePrefix
saveAtmosphereSummary
saveCentroidDetails
saveAcquisitionDiagnostics
parameterRecordIntervalFrames
diagnosticRecordIntervalMs
flushIntervalRecords
maxPendingStorageRecords
writeMetadataJson
imageSavingFixedOff
~~~

保存行为：

- SettingsDialog 打开时从 AppConfig 加载；
- Cancel 丢弃编辑副本；
- Apply 先 validate，再发出 configurationApplied；
- Save/OK 通过 AppConfigPersistence 写 QSettings；
- 数据存储设置页必须作为第五个页面“数据存储”存在；四个功能参数页的标题和顺序保持不变；
- 运行中不能静默改变正在使用的相机节点；
- 需要重启采集的配置改变，显示“需要停止并重新开始采集”的提示；
- 存储路径、文件前缀和存储开关只对下一次 runGeneration 生效；当前运行必须继续写入已打开的文件；
- MainWindow 负责在确认后重新配置相机；
- 物理参数改变后必须清空旧统计，不能用旧物理参数解释新样本。

---

## 11. 任务八：主窗口和控制器接入

### 11.1 MainWindow 职责

MainWindow 负责：

- 创建 AppConfig 和 AppConfigPersistence；
- 创建 QThread、CameraWorker、MeasurementWorker、ResultWriter；
- 连接跨线程信号；
- 打开设置窗口；
- 开始/停止测量；
- 显示连接状态、抓取状态、处理状态和统计状态；
- 显示全画幅预览；
- 显示 ROI 预览和两个 ROI 框；
- 显示 StarA、StarB 质心；
- 显示四个大气输出和样本统计；
- 关闭窗口时执行完整停止顺序。

MainWindow 不负责：

- 直接访问 pylon；
- 直接执行 OpenCV 质心；
- 直接写 CSV；
- 直接改 ROI；
- 直接计算物理公式。

### 11.2 显示更新

实现 ImageDisplayAdapter：

- 将 CameraFrame 转成 GUI 可显示的 QImage；
- 转换前复制图像数据；
- Mono8 使用灰度格式；
- Mono8 显示直接复制到独立 QImage；显示缩放不得改变算法线程中的 Mono8 原图；
- QImage 必须拥有自己的数据或在 signal 发出前完成 copy；
- GUI 只接受受限频率的显示信号：
  - fullFramePreviewRateHz 控制全画幅；
  - roiPreviewRateHz 控制 ROI；
- ROI 叠加图不应从 GUI 线程反向读取处理线程中的可变对象；使用 RoiOverlay 值拷贝。

### 11.3 开始测量前检查

MainWindow::startMeasurement() 按顺序检查：

1. AppConfig.validate；
2. physical 中 D、B、f、wavelength、pixelSize 都有效；
3. measurementRateHz >= 100；
4. 触发模式与相机 capabilities 匹配；
5. 设定的像素格式可用；
6. 硬件 AOI 若启用则相机支持 AOI；
7. outputDirectory 可写或能创建；
8. 生成新的 runGeneration；
9. 清空所有旧队列和统计；
10. 配置 CameraWorker；
11. 配置 MeasurementWorker；
12. 打开 ResultWriter；
13. 启动线程和相机；
14. 状态切换到 Running。

任一步失败必须回滚已启动的对象并给用户明确错误，不得留下半运行状态。

### 11.3.1 从“开始采集”到双星点质心的完整流程

当前 UI_pylon 还是工程骨架，本节描述实现后的目标时序。运行链路只接受真实 Basler pylon 相机，不包含模拟相机或图像回放采集。

~~~text
GUI 点击“开始采集”
    ↓
配置校验、生成 runGeneration、清空旧队列
    ↓
启动 CameraWorker / MeasurementWorker / ResultWriter
    ↓
CameraWorker 连接真实 Basler 相机并配置节点
    ↓
pylon 开始取帧
    ↓
pylon 回调深拷贝 CameraFrame
    ├─→ FrameQueue → MeasurementWorker
    └─→ DisplayMailbox → GUI 低频显示
             ↓
       全画幅找双星
             ↓
       建立 StarA/StarB 软件 ROI
             ↓
       每帧处理两个 ROI
             ↓
       Otsu → 4 连通域 → 小核强度加权质心
             ↓
       双星配对、差分位移、ROI 跟踪和统计
~~~

具体步骤如下。

#### 步骤 A：GUI 线程执行开始前检查

点击 MainWindow 的“开始采集”后，GUI 线程只做轻量的控制和校验，不直接调用 pylon：

1. 检查当前状态是否为 Ready/Idle，防止重复点击创建第二套线程。
2. 调用 AppConfig::validate：
   - D、B、f、波长、像元尺寸有效；
   - measurementRateHz >= 100；
   - r0WindowFrames >= 2，默认 1000；
   - Otsu、连通域、小核和 ROI 参数在合法范围；
   - 触发模式、硬件线路、像素格式和 AOI 选项满足已连接相机能力；
   - storagePath 可创建或可写。
3. 递增 runGeneration。
4. 清空 FrameQueue、DisplayMailbox、AtmosphereWindow 和 ResultWriter 的上一轮数据。
5. 禁用“开始采集”，启用“停止采集”，状态改为 Starting。
6. 通过 Qt::QueuedConnection 向 CameraWorker、MeasurementWorker 和 ResultWriter 发送本轮配置及 generation。

如果校验失败，GUI 只显示缺失字段或错误原因，不能创建相机线程，也不能生成模拟帧。

#### 步骤 B：启动三个工作对象

启动顺序：

1. ResultWriter 在线程内创建本次运行目录和文件：
   - atmosphere_summary.csv；
   - centroid_details.csv；
   - acquisition_diagnostics.csv；
   - run_metadata.json。
2. MeasurementWorker 在线程内 reset：
   - TwoStarTracker 状态设为 FullFrameLocating；
   - 清空 StarA/StarB 历史质心；
   - 清空 ROI 状态；
   - 清空 AtmosphereWindow；
   - 保存本轮配置快照。
3. CameraWorker 在线程内创建并拥有 PylonCamera，调用 PylonInitialize、枚举设备、打开真实 Basler 相机并读取型号/序列号/能力。
4. CameraWorker 按配置设置像素格式、曝光、增益、触发模式、测量采样率和初始全画幅 AOI。
5. 只有相机节点配置成功、ResultWriter 打开成功、处理线程准备完成后，才调用 StartGrabbing。

如果任一环节失败，按统一回滚顺序停止已启动对象、关闭文件、清空队列并恢复“开始采集”按钮。

#### 步骤 C：pylon 取帧和跨线程投递

真实相机输出每一帧后，pylon image event handler 执行：

1. 检查 grab result 是否成功，过滤 incomplete frame。
2. 读取 Width、Height、PixelType、BlockID、Timestamp 和当前 sourceRect。
3. 将 Mono8 图像深拷贝到自有 cv::Mat；不得继续引用 pylon buffer，也不得在本项目中混入 Mono12/Mono12Packed 数据。
4. 填写 CameraFrame：
   - frameId；
   - timestampTicks；
   - hostTimestampNs；
   - width/height；
   - pixelFormat；
   - generation；
   - sourceRect；
   - isFullFrame。
5. 将 CameraFrame 放入有界 FrameQueue，供 MeasurementWorker 使用。
6. 将最新显示副本发布到 DisplayMailbox，供 GUI 定时器使用。
7. 只更新轻量采集统计，不执行 Otsu、connectedComponentsWithStats、质心或 CSV 写入。

FrameQueue 和 DisplayMailbox 的用途不同：

- FrameQueue 服务测量处理，按配置和队列策略统计 processing dropped frame；
- DisplayMailbox 只保留最新图像，避免 100 Hz 图像事件堆积到 GUI；
- GUI 的全画幅预览约 3 Hz、ROI 预览约 20 Hz，不会反向限制真实相机采集。

#### 步骤 D：第一次全画幅定位双星

MeasurementWorker 从 FrameQueue 取得第一批当前 generation 的完整帧。此时 TwoStarTracker 为 FullFrameLocating：

1. 确认 CameraFrame.sourceRect 是全画幅，默认尺寸 1920 x 1200；如果是硬件 AOI，则先请求恢复全画幅。
2. 将图像转换为单通道算法输入：
   - Mono8 直接使用；
   - 不使用暗场模板、热像素 mask 或热像素修正。
3. 对全画幅执行 Otsu 阈值分割。
4. 用 4 连通域提取亮斑候选。
5. 按可配置的最小/最大连通域面积、峰值和候选间距过滤。
6. 选择两个星点候选，保存稳定的 StarA/StarB 编号：
   - 初次按候选位置和基线方向建立编号；
   - 后续重定位按历史全局位置和 baseline projection 匹配；
   - 不使用每次按 x 坐标排序的方式。
7. 以两个候选位置分别生成默认 64 x 64 软件 ROI，clamp 到全画幅边界。
8. 两个 ROI 都有效后，先计算外层硬件 AOI：取 StarA/StarB 两个软件 ROI 的全局包围矩形，四边扩展 `hardwareAoiMarginPx`，再按相机 AOI 增量对齐并裁剪到 1920 x 1200 边界。
9. 通过 queued 控制消息让 CameraWorker 停止当前抓取、应用该单个连续硬件 AOI、重新启动抓取；这段切换期间不产生有效测量样本。
10. CameraWorker 用真实 FrameID、相机时间戳和主机时间戳测量 AOI 模式实际频率；只有实际测量链路达到至少 100 Hz 时，状态才允许进入有效 ROI 测量。若 AOI 不支持、应用失败或实际频率低于 100 Hz，进入 `MeasurementRateInsufficient`，可以继续显示和记录诊断，但不得发布有效四参数。
11. 两个 ROI 都有效但 AOI 尚未应用完成时，状态保持 `HardwareAoiUpdating`；如果只有一个候选或配对失败，继续全画幅重定位，不生成 validPair。

全画幅定位的目标是找出两个星点和建立 ROI，不是直接用全画幅数据计算四个大气参数。

高速测量阶段的画面语义：切换到外层硬件 AOI 后，相机不再同时输出完整 1920 x 1200 图像。全画幅控件使用已知 `sourceRect` 把 AOI 图像放回 1920 x 1200 坐标画布，AOI 之外填充为空并标注“AOI 模式”；这保证 UI 坐标和 ROI 位置连续，同时不为低频预览破坏 100 Hz 测量链路。若后续必须看到真实完整画面，只能在停止测量或显式重定位阶段切回全画幅。

#### 步骤 E：每帧两个 ROI 的质心计算

进入 RoiTracking 后，MeasurementWorker 对每一个当前 generation 的测量帧执行：

1. 根据 CameraFrame.sourceRect 把 StarA/StarB 的全局 ROI 坐标转换为当前帧局部坐标。
2. 分别裁剪 StarA ROI 和 StarB ROI；输入图像必须 clone 为处理线程自有数据。
3. 对每个 ROI 独立调用 CentroidEngine::compute，处理顺序固定为：
   - 检查单通道、尺寸和有限值；
   - 使用 Otsu 分箱数 otsuHistogramBins 计算阈值；
   - 生成 image > otsuThreshold 的二值 mask；
   - 使用 4 连通域得到 label、面积和连通域质心；
   - 按 minComponentAreaPx、maxComponentAreaPx 过滤；
   - 一个有效连通域时直接选择；
   - 多个有效连通域时，使用上一帧 StarA/StarB 全局质心距离选择；
   - 没有上一帧质心时不能随机选取，当前星点结果置为无效；
   - 将选中连通域质心四舍五入为小核中心；
   - 以 smallKernelRadiusPx 为半径，在原始 Mono8 DN 上做强度加权平均；
   - 小核有效像素少于 minSignalPixels 时返回无效；
   - 输出全局质心、峰值、积分强度、Otsu 阈值、连通域面积、小核半径和有效像素数。
4. StarA 和 StarB 都有效时生成 validPair；任意一个无效时本帧只记录无效原因，不进入方差窗口。
5. 用两个全局质心计算 `dx = StarB.x - StarA.x`、`dy = StarB.y - StarA.y`，再按 `baselineAngleDeg` 计算 longitudinalDisplacement 和 transverseDisplacement；这里的像素间距只用于差分样本，不替代 optical baseline B。
6. 用当前质心更新下一帧软件 ROI，并执行边缘、跳变、连续帧和冷却时间判断。
7. 质心明细按 parameterRecordIntervalFrames 投递到 ResultWriter；GUI 只通过 DisplayMailbox 更新 ROI 预览。

#### 步骤 F：质心之后的统计和四参数

每个 validPair 样本进入 AtmosphereWindow：

1. 追加纵向和横向差分位移；
2. 窗口未达到 r0WindowFrames，默认前 999 个有效样本只显示“等待窗口 x/1000”；
3. 第 1000 个有效样本到达后，调用 AtmosphereParameterCalculator 计算 r0、seeing、theta0 和 tau0；
4. 之后每 updateEveryFrames 个测量处理帧重新计算一次最近 1000 个有效样本；
5. 100 Hz、更新周期 1 s 时，updateEveryFrames=100；
6. 每次参数更新前检查 `measurementActualRateHz >= 100`、窗口有效样本数满足 `r0WindowFrames`、物理参数有效且当前不在 AOI 切换/重定位状态；任一条件不满足时只发布无效状态和原因，不生成旧结果冒充当前有效结果。
7. 每次有效参数更新同时写入 atmosphere_summary.csv，并通过低频结果信号更新 GUI；采样率不足时只写入诊断/无效结果记录。
8. MeasurementWorker 逐帧维护统计，但 GUI 不逐帧重绘四个结果卡片。

#### 步骤 G：运行期间 ROI 丢失和重定位

如果星点靠近 ROI 边缘、跳变过大或连续丢失：

1. 当前帧根据规则标记为无效；
2. 连续满足 edgeDistancePx、consecutiveFrames、minimumShiftPx 后更新软件 ROI；
3. 超过 lostFramesBeforeRelocalization 后切换为 StarLostRelocating；
4. 停止将重定位期间的帧计入 AtmosphereWindow；
5. 回到全画幅重新找两个星点；
6. 两个星点同时定位成功后，重新提交 StarA/StarB 两个 ROI；
7. 恢复 RoiTracking，继续积累新的有效窗口。

### 11.4 停止、重启和异常

- Stop 按 3.4 的顺序。
- 配置应用涉及曝光、增益、像素格式、触发方式或硬件 AOI 时，先停止再重新配置。
- 只修改显示频率时可以不停止采集，但仍要通过线程安全配置消息更新。
- 相机断开时：
  - CameraWorker 发出 cameraDisconnected；
  - MeasurementWorker 停止纳入新样本；
  - ResultWriter 写入 run_end_reason；
  - GUI 显示“相机断开”，不自动创建第二个相机实例；
  - 如后续实现重连，必须作为独立任务，不在本轮隐式加入。

---

## 12. 任务九：数据存储和结果文件

### 12.1 参考 DIMM 的存储策略，但适配单相机双星点

参考 UI_2 的以下设计：

- 存储路径可在设置中选择并持久化；
- 运行开始时按时间和运行 generation 建立本次运行的文件名；
- 主结果和详细质心数据分开保存；
- 参数记录间隔可配置；
- 关闭运行时 flush 并关闭文件。

不迁移 UI_2 的以下内容：

- camera1/camera2 字段；
- 双相机 frameIdOffset、timestampOffsetTicks、syncResidual 和同步诊断日志；
- 暗场模板曝光、热像素模板和热像素 mask；
- 环境传感器、上位机通信和自动曝光诊断字段。

新项目一次运行建立一个目录：

~~~text
<storagePath>/
└─ yyyy-MM-dd/
   └─ <filePrefix>_<yyyyMMdd_HHmmss>_g<generation>/
      ├─ atmosphere_summary.csv
      ├─ centroid_details.csv
      ├─ acquisition_diagnostics.csv
      └─ run_metadata.json
~~~

目录创建失败时，开始测量必须失败并在 UI 显示具体路径和系统错误；不能悄悄退回临时目录。

默认存储策略：

- 保存大气参数汇总：开启；每次四参数窗口更新写一行；
- 保存双星质心明细：开启；默认每个处理帧记录一行，记录间隔可配置；
- 保存采集诊断：开启；默认按 1 s 或配置的诊断间隔写入轻量统计；
- 图像保存：固定关闭；本阶段不保存原始全画幅、外层 AOI 或 StarA/StarB ROI 图像；
- DisplayMailbox 中的内存快照仅供 UI 显示，停止运行后释放，不落盘；本轮不增加快照按钮和图像保存队列。

### 12.2 数据存储设置页面

SettingsDialog 增加第五个页面，标题必须是：

数据存储

页面结构参考 DIMM 的布局，分为“存储路径”“记录策略”“结果文件”三个卡片。

存储路径卡片：

- storagePath：默认使用用户可写的数据目录，首次运行显示当前实际路径；
- filePrefix：默认 StarPointMeasurement；
- 浏览按钮使用 QFileDialog::getExistingDirectory；
- 页面显示当前路径是否存在、是否可写；
- 不允许把路径写死为参考工程的 D:/C-DIMM/data。

记录策略卡片：

- saveAtmosphereSummary，默认开启；
- saveCentroidDetails，默认开启；
- saveAcquisitionDiagnostics，默认开启；
- parameterRecordIntervalFrames，默认 1，表示每个处理帧记录双星质心明细；
- diagnosticRecordIntervalMs，默认 1000；
- flushIntervalRecords，默认 100；
- maxPendingStorageRecords，默认 4096；
- writeMetadataJson，默认开启；
- imageSavingFixedOff，固定关闭并显示“本阶段不保存原始全画幅、AOI 或 ROI 图像”；

结果文件卡片：

- 显示本次运行将生成的三个 CSV 文件名；
- 显示“汇总文件按四参数更新写入，质心明细按帧写入，诊断文件按时间间隔写入”；
- 显示当前算法版本和四参数窗口，例如“窗口 1000 帧，默认每秒更新”；
- 不在设置页放双相机同步日志开关。

校验：

- storagePath 不能为空，必须能创建或已经存在且可写；
- filePrefix 不能为空，只允许字母、数字、下划线、连字符和中文，禁止路径分隔符；
- parameterRecordIntervalFrames >= 1；
- diagnosticRecordIntervalMs >= 100；
- flushIntervalRecords >= 1；
- maxPendingStorageRecords >= flushIntervalRecords；
- 至少开启 saveAtmosphereSummary 或 saveCentroidDetails 其中一项；
- 运行中修改存储路径、文件前缀或文件开关，只对下一次 runGeneration 生效，不切换当前已打开的文件。

### 12.3 ResultWriter

ResultWriter 仍是唯一拥有文件句柄的线程，但从一个文件扩展为三个独立输出流。处理线程只投递值语义的 StorageRecord，不执行 QFile/QTextStream 操作。

ResultWriter 至少提供：

- openRun(const StorageConfig&, const RunMetadata&, QString* error)；
- enqueueAtmosphere(const AtmosphereResultRecord&)；
- enqueueCentroid(const CentroidDetailRecord&)；
- enqueueDiagnostic(const AcquisitionDiagnosticRecord&)；
- flush()；
- closeRun()。

每类记录使用固定容量队列；队列满时不能无限等待相机或处理线程。优先保证 atmosphere_summary，centroid_details 和 diagnostics 记录 storageDroppedCount 并在 UI/metadata 中报警；不能静默丢失。

ResultWriter 按 flushIntervalRecords 批量写入并 flush；closeRun 必须按 summary、centroid、diagnostic 顺序 flush、关闭并清空队列。运行结束时写入 run_end_reason、storageDroppedCount 和实际文件路径。

### 12.4 CSV 头

atmosphere_summary.csv 至少写入：

~~~text
run_generation
update_frame_index
timestamp
window_frame_count
valid_window_sample_count
longitudinal_variance_px2
transverse_variance_px2
r0_m
seeing_arcsec
coherence_angle_arcsec
coherence_time_ms
r0_longitudinal_m
r0_transverse_m
seeing_longitudinal_arcsec
seeing_transverse_arcsec
atmosphere_valid
invalid_reason
configured_measurement_rate_hz
actual_measurement_rate_hz
storage_dropped_count
~~~

centroid_details.csv 至少写入：

~~~text
run_generation
frame_id
timestamp_ticks
host_timestamp_ns
source_x
source_y
source_width
source_height
star_a_valid
star_a_x_px
star_a_y_px
star_a_peak_dn
star_a_integrated_intensity
star_a_otsu_threshold
star_a_component_area_px
star_a_small_kernel_radius_px
star_a_used_pixel_count
star_a_roi_x
star_a_roi_y
star_a_roi_width
star_a_roi_height
star_b_valid
star_b_x_px
star_b_y_px
star_b_peak_dn
star_b_integrated_intensity
star_b_otsu_threshold
star_b_component_area_px
star_b_small_kernel_radius_px
star_b_used_pixel_count
star_b_roi_x
star_b_roi_y
star_b_roi_width
star_b_roi_height
valid_pair
dx_px
dy_px
longitudinal_displacement_px
transverse_displacement_px
roi_state
parameter_record_interval_frames
message
~~~

acquisition_diagnostics.csv 至少写入：

~~~text
run_generation
frame_id
camera_timestamp_ticks
host_timestamp_ns
trigger_mode
width
height
source_x
source_y
source_width
source_height
received_frame_count
dropped_grab_frames
dropped_processing_frames
dropped_storage_records
frame_queue_depth
grab_callback_time_us
actual_acquisition_rate_hz
actual_measurement_rate_hz
roi_state
message
~~~

三个文件开头和 run_metadata.json 必须包含本次运行的配置快照：

- 物理/光学参数；
- 采集参数；
- 图像处理参数；
- 触发设置；
- 数据存储参数；
- 相机型号和序列号；
- pylon 版本；
- 算法版本，例如 otsu-connected-component-small-kernel-v1；
- r0WindowFrames、atmosphereUpdateIntervalSeconds、configuredMeasurementRateHz；
- 开始时间、结束时间、runGeneration；
- 文件创建错误、丢帧和丢存储记录计数。

不要只保存四个最终数值，也不要把全画幅图像当作默认数据格式；必须能从 centroid_details.csv 重建每一帧双星质心和 ROI 状态，从 atmosphere_summary.csv 查看每次窗口更新的四参数。

### 12.5 CSV 兼容和写入细节

- 使用 UTF-8 文本；字段中可能包含中文错误信息时进行 CSV 引号和双引号转义；
- 数值使用稳定小数或科学计数格式，NaN/Inf 写为空值并同时写 invalid_reason；
- 每个 CSV 第一行写版本和字段说明，第二行写列名，或使用 run_metadata.json 保存字段版本；格式必须固定并在代码中集中定义；
- 文件写入失败、磁盘空间不足、队列溢出必须通过 queued signal 通知 GUI；
- ResultWriter 不得持有 MainWindow、SettingsDialog 或 ProcessingWorker 指针；
- 运行停止和异常退出都要尝试 flush，不能依靠程序析构时隐式关闭。

---

## 13. 任务十：静态测试和不构建验收

本轮禁止构建，因此测试必须不依赖 C++ 编译产物和实际相机。

### 13.1 tests/test_plan_static.py

检查：

- CMakeLists.txt 包含 Qt6::Widgets；
- CMakeLists.txt 不包含 Qt5::Widgets；
- CMakeLists.txt 包含 pylon::pylon；
- CMakeLists.txt 不包含 GX_ 或 Galaxy SDK 路径；
- CMakeLists.txt 使用 C++17；
- 五个设置页标题存在：物理/光学参数、采集参数、图像处理参数、触发设置、数据存储；
- measurementRateHz 的默认值或校验下限为 100；
- edgeDistancePx=16、consecutiveFrames=5、cooldownMs=3000、minimumShiftPx=8 在配置默认值或代码中存在；
- processing 配置包含 otsuHistogramBins、minComponentAreaPx、maxComponentAreaPx、minSignalPixels、smallKernelRadiusPx、r0WindowFrames、atmosphereUpdateIntervalSeconds；
- 新项目代码不包含 HotPixelTemplate、HotPixelRoiCache、applyHotPixelCorrection、暗场模板路径或热像素 mask 配置；
- 不存在两个 main()；
- UI_pylon 源码不直接引用 CInstantCamera 之外的跨线程 UI 操作；
- 不存在 camera1、camera2 双相机同步字段；
- 不存在 frameIdOffset 或 timestampOffsetTicks 等双相机补偿字段；
- 不存在未完成的任务标记或占位内容。

测试可以使用 pathlib 读取文本；路径固定为当前工程目录的上一级推导路径，不要硬编码用户临时目录。

### 13.2 tests/test_roi_state_machine.py

使用 Python 纯逻辑测试或按同样规则重建最小状态机，验证：

- 一帧靠近边缘不会更新；
- 连续 5 帧靠近边缘且位移超过 8 px 才更新；
- cooldownMs 内再次靠近边缘不更新；
- 一颗星连续丢失 10 帧进入全画幅重定位；
- 全画幅定位必须同时得到 StarA 和 StarB 才能回到 RoiTracking；
- 配对失败不会生成有效样本；
- 使用 baseline 投影后星点编号稳定。

### 13.3 tests/test_physics_calculator.py

只验证公式输入输出约束和单位换算：

- D、B、f、wavelength、pixelSize 为正时可计算；
- B=0、f=0、方差=0、NaN、Inf 返回无效；
- 5.86 um 和指定焦距的 pixelScaleRad 数量级合理；
- 纵向和横向公式使用不同的基线系数；
- 只有 validPair 样本进入方差；
- 有效样本少于 2 时不生成有效方差。

同时验证 AtmosphereWindow：

- r0WindowFrames=1000 时，前 999 个有效样本不生成有效四参数；
- 第 1000 个有效样本生成第一次结果；
- measurementRateHz=100、atmosphereUpdateIntervalSeconds=1 时，每 100 个测量处理帧更新一次；
- 新样本进入后移除最旧样本，窗口始终不超过 1000 个有效样本；
- 修改窗口大小或更新周期后清空旧窗口；
- 无效双星帧不进入方差，但会增加 invalid frame 计数。

### 13.4 允许执行的检查

允许执行：

~~~powershell
python tests/test_plan_static.py
python tests/test_roi_state_machine.py
python tests/test_physics_calculator.py
~~~

如果使用 pytest，也只允许执行：

~~~powershell
python -m pytest tests -q
~~~

不允许执行：

- cmake -S ...
- cmake -B ...
- cmake --build ...
- MSBuild ...
- ninja ...
- make ...
- devenv ...
- 任何会产生 C++ 构建输出的命令。

---

## 14. 推荐执行顺序

低性能 agent 必须按以下顺序执行，每完成一项先检查文件是否自洽：

1. 建立 src 目录内的公共类型和空接口。
2. 修改 CMakeLists.txt，但不配置、不构建。
3. 实现 AppConfig、QSettings 持久化和五页 SettingsDialog，其中第五页为数据存储。
4. 实现 FrameQueue 和线程停止协议。
5. 实现 PylonCamera 的生命周期、能力探测、参数配置和帧深拷贝。
6. 实现 CameraWorker 的连续、软件、硬件触发。
7. 迁移 CentroidLogic 和实现 CentroidEngine。
8. 实现 TwoStarTracker 的全画幅双星定位、稳定编号和两个软件 ROI。
9. 实现 UI_2 风格的动态 ROI 状态机。
10. 实现 AtmosphereParameterCalculator。
11. 实现 MeasurementWorker 的差分、在线方差和采集结束条件。
12. 实现 ImageDisplayAdapter 和 MainWindow 信号连接。
13. 实现数据存储页面、三类 CSV、run_metadata.json、ResultWriter 和 CSV metadata。
14. 增加三个 Python 静态测试。
15. 只运行 Python 静态测试。
16. 向用户交付修改文件列表、静态测试输出和已知需要人工构建/硬件验证的项目。

不要为了绕过编译错误而把所有实现塞回 UI_New.cpp；若遇到 C++ 接口问题，应优先保持线程边界和模块边界，再修正头文件依赖。

---

## 15. 人工验收清单

用户后续自行构建后，按下面顺序验收：

### 15.1 不启动采集的界面验收

- 程序能打开主窗口。
- 设置窗口有五个目标页面：四个功能参数页面加“数据存储”，标题完全一致。
- 五页参数均能读取、修改、Apply、Cancel、重新打开后保持。
- measurementRateHz 输入 99 时不能开始测量。
- B 或 f 为 0 时不能开始有效测量，并明确提示。
- 触发页面能看到连续、软件、硬件三种模式的区别。
- ROI 重定位参数显示 16、5、3000、8 默认值。
- 图像处理参数页可以修改 Otsu 分箱数、连通域面积、小核半径、r0WindowFrames 和四参数更新周期，并且重新打开设置后保持。
- r0WindowFrames 设置为 1000、measurementRateHz 设置为 100 Hz、更新周期设置为 1 s 时，界面显示“窗口 1000 帧，约每 100 帧更新”。
- 数据存储页可以选择路径、修改文件前缀、记录间隔、flush 批量和三个记录开关；保存全画幅/ROI 图像保持关闭。
- 开始一次运行后生成 atmosphere_summary.csv、centroid_details.csv、acquisition_diagnostics.csv 和 run_metadata.json；停止后文件可打开且内容完整。
- 打开设置窗口不会启动采集，也不会因打开设置窗口创建相机线程或访问 pylon；这只是界面验收条件，不代表产品提供模拟采集模式。

### 15.2 接相机但不测量的验收

- 能枚举并显示相机型号、序列号和能力。
- 连续模式能取到图像。
- 软件触发模式下，只有 ExecuteSoftwareTrigger 后才有一帧。
- 硬件触发模式不发送软件 trigger。
- 不支持的触发线路、像素格式、AOI 能被识别并给出错误。
- GUI 不会因为回调线程更新图像而崩溃。

### 15.3 全画幅定位验收

- 全画幅显示频率约为配置值，几 Hz 即可。
- 在全画幅中找到两个星点时显示 StarA、StarB。
- 两个 ROI 同时出现，编号稳定。
- 少于两个候选或配对失败时不生成 validPair。

### 15.4 ROI 跟踪验收

- 星点缓慢移动时，两个软件 ROI 能跟随。
- 单帧靠边不会马上跳 ROI。
- 连续 5 帧满足条件后才更新。
- 更新后 3000 ms 冷却期内不会重复更新。
- 任意一个星点丢失达到阈值，状态显示全画幅重定位。
- 全画幅重新找到两个星点后，两者 ROI 同时恢复。
- 开启硬件 AOI 时，外层 AOI 始终包围两个软件 ROI，移动期间无数据竞争。

### 15.5 100 Hz 测量验收

- 采集频率和 GUI 显示频率相互独立。
- 100 Hz 采集时全画幅预览仍可保持几 Hz。
- ROI 预览频率高于全画幅预览。
- 输出中有实际采样率、有效样本数、无效样本数和丢帧数。
- 2000 样本或 20 s 到达后正常结束。
- 停止按钮不会死锁，线程能全部退出。
- 重复开始/停止不会创建重复线程或重复文件句柄。

### 15.6 物理结果验收

- 结果文件记录本次物理参数快照。
- 质心坐标和差分位移单位正确。
- 只有成对有效帧进入方差。
- 缺少 B 或 f 时不会输出看似有效的 r0、seeing。
- 后续替换棱镜双窗口物理公式时，只需要修改 AtmosphereParameterCalculator 和对应测试。

---

## 16. UI 布局与交互设计规范

本节是对 UI_2/DIMM 现有界面的适配性延伸。执行 agent 必须参考 DIMM 的深色背景、顶部标签导航、卡片分组、青色选中态和状态色，但不能照搬双相机、EAF 或自动采集相关控件。新界面围绕“一台相机、两个星点、一个测量结果链路”组织。

### 16.1 设计上下文

- 目标平台：Windows 桌面 Qt Widgets 应用。
- 主窗口形状：横向矩形；推荐工作尺寸 1600 x 900，最低可用尺寸 1280 x 720。
- 必须区分软件窗口尺寸和相机图像尺寸：当前 acA1920-40gm 的传感器全分辨率是 1936 x 1216，但本项目第一版固定使用官方默认画幅 1920 x 1200；1600 x 900 和 1280 x 720 只是 UI 窗口尺寸，不是采集分辨率。
- 本项目以 1920 x 1200 作为全画幅初始定位和重定位输入，不在第一版设置中开放 1936 x 1216 切换。全画幅尺寸只影响搜索范围和显示，不改变 ROI 内质心的像素坐标定义，也不改变 DIMM 公式；质心到角度的换算仍由实际像元尺寸和焦距完成。预览只允许在显示控件中等比例缩放和加留白，不得裁剪、拉伸或把显示尺寸当成采集分辨率。
- 语言：中文为主，参数名后保留英文单位或 SDK 枚举名。
- 输入：鼠标和键盘；所有按钮、组合框、输入框必须支持 Tab 顺序和 Enter/Escape 操作。
- 观看距离：桌面约 60 cm；正文和关键数值不得使用过小字体。
- 内容优先级：当前测量状态和四个结果最高，其次是双星点图像与 ROI 状态，再其次是相机细节和高级配置。
- 不引入 Qt Quick；使用现有 Qt Widgets、QMainWindow、QTabWidget、QGroupBox、QFormLayout、QSplitter 和 QStatusBar。

### 16.2 DIMM 风格适配原则

沿用 DIMM 的以下视觉特征：

- 深海军蓝主背景，略深的内容卡片，细边框分隔；
- 顶部横向导航/设置页，当前页使用青色底边或青色高亮；
- 参数按功能放入卡片分组，标签左对齐、控件右侧对齐；
- 状态用颜色加文字和图标/形状共同表达，不只依靠颜色；
- 重要数值使用较大字号，普通说明使用次级文字颜色；
- 控件圆角保持克制，避免大面积渐变、阴影和装饰动画；
- 保持 DIMM 的紧凑数据密度，但新项目的结果区域要比设置区域更突出。

建议在 src/UiTheme.h 和 src/UiTheme.cpp 中集中定义并应用以下设计令牌，禁止在 MainWindow.cpp 和 SettingsDialog.cpp 中散落大量颜色字符串：

- surfaceWindow：#081426；
- surfacePanel：#0E1B2E；
- surfaceInput：#0B182A；
- outline：#294665；
- accentCyan：#27D7FF；
- textPrimary：#EDF5FF；
- textSecondary：#93A9C3；
- success：#52D273；
- warning：#FFC857；
- error：#FF6376；
- starA：#FFD166；
- starB：#7DD3FC。

UiTheme::apply(QApplication&) 负责应用全局 QPalette/QSS；UiTheme::styleMainWindow、UiTheme::styleSettingsDialog 或等效函数负责少量页面级样式。不要把业务状态写进固定颜色；通过 QLabel 属性或状态枚举选择语义色，并同时更新文字内容。

### 16.3 主窗口布局

MainWindow 使用三层布局：顶部状态栏、中央测量区、底部操作/日志区。

~~~text
┌──────────────────────────────────────────────────────────────────────────────┐
│ 标题/设备连接状态 │ 触发模式 │ 采集率 │ ROI状态 │ 设置 │ 开始/停止              │
├───────────────────────────────────────┬──────────────────────────────────────┤
│                                       │ 四个大气结果卡片                     │
│                                       │ r0 / seeing                          │
│       全画幅预览                      │ theta0 / tau0                        │
│       StarA/StarB 标注                │ 样本进度 / 有效样本 / 丢帧           │
│       两个ROI框和质心十字线            │ 三种频率实际值                       │
│                                       │ ROI状态和重定位提示                 │
├───────────────────────────────────────┤                                      │
│ ROI A 预览       │ ROI B 预览          │                                      │
│ 质心/峰值/有效性 │ 质心/峰值/有效性    │                                      │
├───────────────────────────────────────┴──────────────────────────────────────┤
│ 状态消息/错误详情 │ 全画幅重定位 │ 打开结果目录 │ 停止/关闭                     │
└──────────────────────────────────────────────────────────────────────────────┘
~~~

具体布局要求：

- 顶部高度建议 48 到 56 px；始终显示相机连接、当前触发模式、采集状态和 ROI 状态。
- 中央区域用 QSplitter，左侧约 62% 到 68% 宽度，右侧约 32% 到 38% 宽度；允许用户拖动，但启动时采用上述默认比例。
- 全画幅预览是主视觉区域，默认按 1920 x 1200 的 16:10 比例显示；进入硬件 AOI 后仍使用该坐标画布显示并标注 AOI 模式。使用 KeepAspectRatio 等比例缩放，必要时在上下或左右留黑边，不能让结果卡片挤压到无法阅读。
- 全画幅预览频率只控制 GUI 刷新频率，不改变默认 1920 x 1200 的初始定位画幅；进入高速测量后 CameraFrame.sourceRect 是外层硬件 AOI，显示控件按 sourceRect 将 AOI 画面映射回 1920 x 1200 坐标画布并明确标注“AOI 模式”。
- 右侧结果区使用垂直布局：四个结果卡片优先，其次是样本进度和实际频率，再其次是状态/错误详情。
- 四个结果卡片固定显示 r0、seeing、theta0（相干角）和 tau0（相干时间）；纵向/横向方差与分量放在可展开的诊断区域，不占用主结果卡片。
- 两个 ROI 预览必须并排出现，使用相同尺寸和相同缩放规则；左侧固定标记 StarA，右侧固定标记 StarB。
- ROI 预览上显示质心十字线、峰值强度、有效/无效文字和当前 ROI 尺寸；无效时保留窗口但显示“未找到”与原因，不能让布局跳动。
- 底部状态区只显示一行摘要；较长错误通过可展开的详细信息区域显示，不用弹出大量模态框遮挡实时图像。
- 主窗口关闭、停止测量和应用需要重启采集的配置时，才弹出确认框；普通状态变化不弹模态框。

### 16.4 顶部状态和主要按钮

顶部左侧显示：

- 应用名称：KY-DIMM 双星点大气相干长度测量仪；
- 相机连接徽标：未连接 / 已连接 / 采集中 / 相机错误；
- 相机型号和序列号可放在次级文本，不抢占主标题。

顶部中部显示：

- 触发模式：连续采集 / 软件触发 / 硬件触发；
- 采集状态：待机 / 全画幅定位 / ROI跟踪 / ROI重定位 / 停止中 / 错误；
- 这些状态必须同时有文字，不能只显示一个圆点。

顶部右侧按钮顺序固定为：

1. 设置：打开五页 SettingsDialog；
2. 开始测量：待机且配置通过校验时启用；
3. 停止测量：运行中替换开始按钮，使用 error 或 warning 语义色；
4. 窗口关闭：执行完整线程停止顺序。

“全画幅重定位”作为中央区下方的次级按钮。它只在相机已采集时启用，点击后通过线程安全消息请求 TwoStarTracker 进入 FullFrameLocating，不得由 GUI 线程直接修改 ROI。

### 16.5 设置窗口的视觉和布局

SettingsDialog 继续采用 DIMM 风格的顶部 QTabWidget，前四页顺序和标题必须是：

1. 物理/光学参数
2. 采集参数
3. 图像处理参数
4. 触发设置

第五页为“数据存储”。它参考 DIMM 的顶部标签和卡片样式，但只显示单相机双星点项目需要的路径、记录策略和结果文件信息。

每个页面采用“页面标题 + 一到三个卡片分组 + 页面底部说明”的结构。页面宽度建议 980 到 1180 px；图像处理参数和数据存储参数较多时使用页面内部滚动区域，顶部标签固定不滚动。

通用控件规范：

- 参数标签宽度统一，单位显示在控件右侧或 QDoubleSpinBox suffix 中；不要让单位单独漂浮在另一列。
- 数值输入使用 QDoubleSpinBox/QSpinBox，设置明确的 min、max、decimals、singleStep；禁止用纯 QLineEdit 接受无校验数字。
- 枚举使用 QComboBox，显示中文名称，内部保存稳定英文键值。
- 布尔项使用 QCheckBox，标签写成“启用硬件 AOI”等完整动作语句。
- 需要能力探测的选项旁边显示“相机不支持”或禁用状态，同时保留说明 tooltip；不能静默隐藏导致用户不知道为什么没有选项。
- 页面底部固定按钮区：恢复默认、应用、确定、取消；确定和取消顺序保持一致，默认焦点给确定或应用中的主操作。
- Apply 成功后显示一条短暂但可读的状态提示；需要停止重启时显示明确提示，不自动偷偷重启。

前四个功能页面的卡片分组：

物理/光学参数：

- 相机与像元：像元尺寸、水平/垂直像素；
- 双窗口物理关系：子孔径 D、窗口中心距 B、基线方向角；棱镜仅用于生成两个相机光斑，不在设置中建模；
- 成像几何：焦距、波长、天顶角；
- 右侧或底部放单位说明和物理参数默认值提示；当前 B=150 mm、f=2500 mm，参数必须可编辑。

采集参数：

- 相机曝光与增益；
- 三种频率：全画幅预览、ROI 预览、测量采集/处理；
- 样本数、持续时间、像素格式；
- 空间采集：软件 ROI、可选硬件 AOI、外层 AOI margin、全画幅重定位周期；
- 以信息卡明确展示：全画幅显示约 3 Hz、ROI 显示约 20 Hz、测量采样至少 100 Hz，三者互不等价；即使全画幅 UI 只刷新 3 Hz，测量线程也必须持续消费 AOI 帧并保持至少 100 Hz 的实际速率。

图像处理参数：

- 质心算法和阈值；
- 亮斑候选过滤；
- StarA/StarB ROI 尺寸和星点间距；
- ROI 重定位参数使用单独的“ROI 重定位参数”卡片，默认值 16、5、3000、8 直接可见；
- 底部放状态机说明和两个 ROI 的示意小图或文字说明，不放复杂装饰。

触发设置：

- 顶部用三选一 QComboBox 或单选按钮明确选择连续采集、软件触发、硬件触发；
- 下方动态显示对应参数：软件触发显示触发频率，硬件触发显示线路和边沿，连续采集显示相机帧率；
- 页面右侧或底部固定说明“触发方式决定何时取一帧；ROI/AOI 决定每帧取多大区域”；
- 当前相机不支持的线路显示为禁用，并在状态文字中说明原因。

数据存储：

- 存储路径卡片包含路径输入框和浏览按钮；
- 记录策略卡片包含汇总、质心明细、采集诊断开关和记录间隔/flush/队列参数；
- 结果文件卡片显示本次运行将生成的三个 CSV 和一个 JSON 文件；
- 页面明确提示默认不保存连续全画幅和 ROI 图像；
- 采用与 DIMM 一致的深色卡片、青色当前页和底部 Apply/OK/Cancel 操作区。

### 16.6 图像叠加和状态可视化

全画幅和 ROI 预览必须使用以下稳定标记：

- StarA：黄色标记、A 标签、实线 ROI 边框；
- StarB：浅蓝色标记、B 标签、实线 ROI 边框；
- 外层硬件 AOI：青色虚线；
- 当前质心：十字线加坐标文本；
- 全画幅重定位：橙色半透明提示条“正在全画幅重定位，暂不计入新样本”；
- 错误：红色提示条加错误文字；
- 无效星点：使用叉号或斜线纹理加“无效”，不只把颜色改红。

三种频率在右侧使用三个独立的小卡片：

- Full preview：目标值、实际值、状态；
- ROI preview：目标值、实际值、状态；
- Measurement：目标值、实际值、丢帧数。

实际值低于目标值时显示 warning；measurement 实际频率低于 100 Hz 时显示 error，并在测量结果中标记“采样率不足”。

结果卡片必须同时显示数值、单位和有效性：

- r0：m；
- seeing：arcsec；
- theta0/相干角：arcsec；
- tau0/相干时间：ms。

结果卡片上方或下方显示“计算窗口：1000 帧，更新：每 100 帧/约 1 s”这类动态文字；实际更新帧数按当前 measurementRateHz 自动替换，不能写死 100。

如果物理参数不完整、有效样本不足、测量实际频率低于 100 Hz、硬件 AOI 未成功应用或 ROI 正在重定位，结果卡片显示“无有效结果”及原因，不显示 0.00 造成误解。

### 16.7 状态流转对应的界面状态

- Disconnected：主图显示相机未连接，占位提示；开始按钮禁用；设置仍可打开。
- Ready：显示“准备就绪”；开始按钮启用；物理参数缺失时显示缺失字段列表。
- FullFrameLocating：主图保留实时画面，显示黄色定位提示；结果卡片显示等待双星点。
- RoiTracking：显示两个 ROI、质心和实时结果；开始按钮替换为停止。
- RecenterPending：显示“准备重定位”，不立即闪烁或移动整个布局。
- StarLostRelocating：显示橙色重定位状态；保留 last-known 数值但加“仅供参考，不计入当前样本”标记。
- HardwareAoiUpdating：显示短暂 AOI 更新提示；旧 ROI 结果冻结但不再添加新样本。
- MeasurementRateInsufficient：显示“测量实际频率不足 100 Hz”；允许预览和诊断继续，但四个参数全部标记无效。
- Stopping：所有开始/设置中会改变采集链路的按钮暂时禁用，状态区显示停止进度。
- Error：顶部显示错误状态和可读原因；提供重试或返回待机路径，不自动重复启动。

### 16.8 Qt 文件级修改要求

执行 agent 必须落实以下代码修改：

- 新增 src/UiTheme.h 和 src/UiTheme.cpp，集中定义颜色、间距、字体层级、QPalette/QSS 和状态色映射；同步加入 CMakeLists.txt。
- 新增 src/DisplayMailbox.h 和 src/DisplayMailbox.cpp，集中处理全画幅、ROI 和 overlay 的 latest-only 跨线程显示数据；同步加入 CMakeLists.txt。
- 将 MainWindow.ui 改为中央 QSplitter + 全画幅显示区 + 右侧结果区 + 双 ROI 预览区 + 底部状态/操作区；不要继续使用空白模板布局。
- 将 SettingsDialog.ui 改为五页 QTabWidget，并按 16.5 的卡片分组放置控件；前四个页面标题和第五个“数据存储”标题必须使用确定的中文字符串。
- 在 MainWindow.cpp 中只做状态到控件的映射，不能在 UI 槽函数中实现相机、质心、ROI 或物理公式。
- 为关键 QLabel、QPushButton、QSpinBox、QComboBox 设置 objectName，便于后续自动化测试和定位，例如 statusCameraLabel、statusRoiLabel、fullFrameView、roiAView、roiBView、resultR0LongitudinalLabel、measurementRateLabel。
- 为每个输入控件设置 accessibleName 或清晰的 label buddy；Tab 顺序按从上到下、从左到右设置。
- 所有状态至少由文字 + 颜色或图标/形状表达；StarA/StarB 不得只靠红蓝颜色区分。
- 关键文字使用 Qt pointSize 或随系统 DPI 缩放的字体；普通正文建议 10 到 12 pt，结果数值建议 16 到 22 pt，顶部状态建议 10 到 12 pt。
- 确保窗口缩放到 1280 x 720 时，四个结果卡片、两个 ROI 预览和开始/停止按钮仍可见；次级日志可以折叠或滚动。
- 不增加无实际功能的动画；若使用状态过渡，持续时间不超过 300 ms，并保证停止/错误状态立即可见。

### 16.9 UI 验收补充

执行 agent 除了第 15 节的功能验收，还必须确认：

- 主界面视觉上继承 DIMM 的深色卡片和青色选中态，但不再出现双相机、EAF、环境传感器等无关入口；
- 全画幅预览明显大于 ROI 预览，右侧四个结果数值优先级最高；
- 用户不看日志也能知道当前是连续、软件还是硬件触发；
- 用户不看代码也能理解全画幅预览、ROI 预览和测量频率是三个独立值；
- ROI 重定位时界面明确提示“暂不计入新样本”，不会让旧值看起来像实时有效结果；
- 1280 x 720、1600 x 900 两种窗口尺寸下无关键控件被裁切；
- Tab 键可以依次访问设置页控件和主窗口主要按钮，焦点可见；
- 错误、警告、无效结果同时有文字说明，不依赖颜色辨识。

## 17. 交付时必须报告的内容

执行 agent 完成任务后，必须报告：

- 修改和新增的文件列表；
- 数据存储页面的字段、默认值和本次运行生成的四个文件；
- 说明哪些数据参考 DIMM 保存、哪些双相机同步/暗场/热像素数据明确未迁移；
- CMakeLists.txt 的关键变更；
- pylon 相机线程的所有权和停止顺序；
- 三种触发模式的实现差异；
- 全画幅预览、ROI 预览、测量采集三个频率的实际语义；
- 动态 ROI 状态机和默认参数；
- 物理参数配置键和默认值；
- 静态测试命令和结果；
- 明确说明没有执行构建；
- 明确列出尚未做的真实相机、硬件触发和 100 Hz 现场测试。
