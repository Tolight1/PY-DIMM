# KY-DIMM Implementation Plan

> **For agentic workers:** Before executing this plan, read `C:/Users/lenovo/.agents/skills/executing-plans/SKILL.md` completely. Execute one task at a time, keep the changes scoped to this repository, and do not build the project unless the owner explicitly asks for it.

**Goal:** Build the first KY-DIMM implementation by selectively migrating the DIMM measurement logic into the current Basler-pylon project, while keeping camera acquisition, measurement, display, settings, and file writing in separate responsibilities.

**Architecture:** A Basler `Mono8` acquisition worker owns pylon objects and produces bounded camera frames. A measurement worker consumes frames, locates two Polaris spots, maintains two moving software ROIs inside one hardware AOI, computes centroids, accumulates a frame-count-based rolling window, and publishes atmosphere results. The Qt GUI only consumes latest snapshots through queued signals/timers and never performs camera I/O or per-frame image processing.

**Tech Stack:** C++17, Qt 6 Widgets, Basler pylon SDK, OpenCV core/imgproc/imgcodecs, CMake. The source tree is `src/`; test helpers are `tests/`; documentation is under `docs/`.

## Non-negotiable execution rules

1. Do not run CMake configure, MSBuild, Visual Studio build, Ninja, Make, or any executable that starts the camera.
2. Only run static inspections, Python unit tests, and `git diff --check` if the repository is actually a Git worktree.
3. Do not migrate DIMM dark-field templates, hot-pixel correction, simulated acquisition, Gaussian-fit centroiding, or the old DIMM camera code.
4. The full pipeline is `Mono8`. Do not silently convert the camera acquisition to `Mono16`.
5. The physical baseline used by the formulas is the distance between the two telescope circular-window centers: `150 mm`. It is not the software ROI distance and it is not a prism-equivalent baseline.
6. The first implementation uses the Basler default image size `1920 x 1200`. Do not add a user-selectable `1936 x 1216` mode in this iteration.
7. A measurement rate below `100 Hz` is an invalid measurement state. It may still show preview images, but it must not publish valid `r0`, seeing, `theta0`, or `tau0` values.
8. Do not save raw full-frame or raw ROI images. CSV/JSON result logging is allowed.

## Fixed initial values

These are defaults, not hard-coded constants. Every item marked configurable must be editable in the settings dialog and persisted through `QSettings`.

| Group | Parameter | Initial value | Meaning |
|---|---|---:|---|
| Physical/optical | Main telescope aperture | `254 mm` | LX200-ACF 10-inch clear aperture |
| Physical/optical | Sub-aperture diameter `D` | `60 mm` | Diameter used by the DIMM coefficient |
| Physical/optical | Window-center baseline `B` | `150 mm` | Distance between the two telescope circular-window centers |
| Physical/optical | Focal length | `2500 mm` | LX200-ACF 10-inch focal length |
| Physical/optical | Wavelength | `550 nm` | Measurement wavelength |
| Camera | Pixel size | `5.86 um` | Basler acA1920-40gm sensor pixel pitch |
| Camera | Pixel format | `Mono8` | Fixed for the first version |
| Camera | Image size | `1920 x 1200` | Initial full-frame acquisition/locating canvas |
| Image processing | Software ROI size | `64 x 64 px` | Each star ROI is measured in camera pixels |
| Image processing | Hardware AOI margin | `64 px` | Margin around the two software ROIs |
| Acquisition | Measurement rate minimum | `100 Hz` | Hard validity gate |
| Acquisition | Full-frame preview rate | `3 Hz` | GUI display only |
| Acquisition | ROI preview rate | `20 Hz` | GUI display only |
| Atmosphere | r0 calculation window | `1000 valid paired frames` | First result appears after this many samples |
| Atmosphere | Result update interval | `1 s` | At 100 Hz, update every 100 valid frames after warm-up |
| Atmosphere | Target sample count | `2000` | Default run target; configurable |
| Atmosphere | Target duration | `20 s` | Default run duration; configurable |

The 10-inch aperture/focal-length defaults must remain independently editable because the telescope model may be confirmed or changed later. The prism is only an optical mechanism that produces two spots; no prism geometry belongs in the formula path.

## File map

Create or modify these files. Do not introduce a second parallel configuration system or duplicate the physics formulas in the UI.

| File | Responsibility |
|---|---|
| `CMakeLists.txt` | Qt6/OpenCV/pylon dependency and source registration |
| `src/main.cpp` | Qt application entry point |
| `src/CameraTypes.h` | Camera-frame, capability, statistics, and ROI data contracts |
| `src/ProcessingTypes.h` | Physical, acquisition, trigger, processing, result data contracts |
| `src/AppConfig.h/.cpp` | Defaults, validation, QSettings persistence |
| `src/FrameQueue.h/.cpp` | Bounded producer/consumer queue for measurement frames |
| `src/DisplayMailbox.h/.cpp` | Latest-only display snapshots; prevents GUI backlog |
| `src/PylonCamera.h/.cpp` | The only module that owns pylon camera objects |
| `src/CameraWorker.h/.cpp` | Acquisition-thread lifecycle and frame production |
| `src/CentroidEngine.h/.cpp` | Otsu + 4-connected components + small-kernel centroid |
| `src/TwoStarTracker.h/.cpp` | Full-frame locating, StarA/StarB identity, ROI tracking, AOI requests |
| `src/AtmosphereCalculator.h/.cpp` | DIMM-compatible four-parameter calculation |
| `src/MeasurementWorker.h/.cpp` | Measurement-thread pipeline and frame-rate validity gate |
| `src/ResultWriter.h/.cpp` | CSV/JSON result records; no image files |
| `src/SettingsDialog.h/.cpp/.ui` | Five required settings pages |
| `src/MainWindow.h/.cpp/.ui` | Main UI, display timers, controls, result cards |
| `src/UiTheme.h/.cpp` | Reuse/adapt the DIMM dark visual language |
| `src/ImageDisplayAdapter.h/.cpp` | Maps camera images and overlays to Qt widgets |
| `tests/test_plan_static.py` | Static architecture and forbidden-feature checks |
| `tests/test_physics_calculator.py` | Formula/units/variance contract checks |
| `tests/test_roi_state_machine.py` | Pure ROI tracking state-machine checks |

## Task 1 — Replace the project skeleton and CMake configuration

### Files

- Modify `CMakeLists.txt`.
- Modify `src/main.cpp`.
- Remove old source registration that depends on `UI_New`.
- Do not delete user files outside the current project.

### CMake requirements

Use one explicit source list so a slower agent can see every compilation unit. Use Qt 6 and the pylon development installation selected by the owner. Adapt only the exact Qt/OpenCV installation paths if the local machine uses different paths; do not downgrade to Qt5.

```cmake
cmake_minimum_required(VERSION 3.21)
project(KY_DIMM LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# Keep these paths configurable at configure time. Do not put pylon DLLs in src/.
set(CMAKE_PREFIX_PATH "E:/Softwoare/Qtool/qt/6.11.0/msvc2022_64" CACHE PATH "Qt prefix")
set(OpenCV_DIR "E:/Softwoare/OpenCV/opencv4120/build" CACHE PATH "OpenCV CMake directory")
set(pylon_DIR "E:/Softwoare/Basler pylon/Development/lib/cmake/pylon" CACHE PATH "Basler pylon CMake directory")

find_package(Qt6 REQUIRED COMPONENTS Widgets)
find_package(OpenCV REQUIRED COMPONENTS core imgproc imgcodecs)
find_package(pylon 12.2.1 REQUIRED)

set(KY_DIMM_SOURCES
    src/main.cpp
    src/CameraTypes.h
    src/ProcessingTypes.h
    src/AppConfig.h
    src/AppConfig.cpp
    src/FrameQueue.h
    src/FrameQueue.cpp
    src/DisplayMailbox.h
    src/DisplayMailbox.cpp
    src/PylonCamera.h
    src/PylonCamera.cpp
    src/CameraWorker.h
    src/CameraWorker.cpp
    src/CentroidEngine.h
    src/CentroidEngine.cpp
    src/TwoStarTracker.h
    src/TwoStarTracker.cpp
    src/AtmosphereCalculator.h
    src/AtmosphereCalculator.cpp
    src/MeasurementWorker.h
    src/MeasurementWorker.cpp
    src/ResultWriter.h
    src/ResultWriter.cpp
    src/SettingsDialog.h
    src/SettingsDialog.cpp
    src/SettingsDialog.ui
    src/MainWindow.h
    src/MainWindow.cpp
    src/MainWindow.ui
    src/UiTheme.h
    src/UiTheme.cpp
    src/ImageDisplayAdapter.h
    src/ImageDisplayAdapter.cpp
)

add_executable(KY_DIMM WIN32 ${KY_DIMM_SOURCES})

target_include_directories(KY_DIMM PRIVATE ${OpenCV_INCLUDE_DIRS})
target_link_libraries(KY_DIMM PRIVATE Qt6::Widgets ${OpenCV_LIBS} pylon::pylon)

if (MSVC)
    target_compile_options(KY_DIMM PRIVATE /W4 /permissive-)
else()
    target_compile_options(KY_DIMM PRIVATE -Wall -Wextra -Wpedantic)
endif()
```

If the installed pylon package exports a target name different from `pylon::pylon`, document the actual target in the task report and change only that link target after checking the SDK's CMake package. Do not add a hand-written DLL path or copy DLLs into the repository.

### `src/main.cpp`

```cpp
#include "MainWindow.h"
#include "UiTheme.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    UiTheme::apply(app);

    MainWindow window;
    window.show();
    return app.exec();
}
```

### Acceptance checks

- No `UI_New` include or construction remains.
- No `Qt5::`, `Qt5Widgets`, or MinGW-specific Qt path remains in `CMakeLists.txt`.
- The file list names every new `.cpp`, `.h`, and `.ui` file.
- Do not configure or build.

## Task 2 — Define shared data contracts before implementing workers

### `src/CameraTypes.h`

Do not put pylon types in this header. The rest of the application receives neutral OpenCV/Qt data.

```cpp
#pragma once

#include <QRect>
#include <QString>
#include <opencv2/core.hpp>

#include <cstdint>

enum class PixelFormat {
    Mono8,
    Unknown
};

enum class TriggerMode {
    Continuous,
    Software,
    Hardware
};

struct RoiRect {
    int x = 0;
    int y = 0;
    int width = 64;
    int height = 64;

    QRect toQRect() const { return QRect(x, y, width, height); }
    bool isValid() const { return width > 0 && height > 0; }
};

struct CameraFrame {
    cv::Mat mono8;
    QRect sourceRect{0, 0, 1920, 1200};
    std::uint64_t sequence = 0;
    std::int64_t timestampNs = 0;
    PixelFormat pixelFormat = PixelFormat::Unknown;
    std::uint64_t configurationGeneration = 0;

    bool isValid() const
    {
        return !mono8.empty() && mono8.type() == CV_8UC1 &&
               sourceRect.width() == mono8.cols &&
               sourceRect.height() == mono8.rows &&
               pixelFormat == PixelFormat::Mono8;
    }
};

struct CameraCapabilities {
    QString modelName;
    QString serialNumber;
    int sensorWidth = 1936;
    int sensorHeight = 1216;
    int defaultWidth = 1920;
    int defaultHeight = 1200;
    double pixelSizeUm = 5.86;
    double nominalFrameRateHz = 42.0;
    bool supportsMono8 = false;
    bool supportsSoftwareTrigger = false;
    bool supportsHardwareTrigger = false;
    bool supportsHardwareAoi = false;
};

struct CameraStatistics {
    std::uint64_t receivedFrames = 0;
    std::uint64_t droppedFrames = 0;
    std::uint64_t queueDroppedFrames = 0;
    double measuredRateHz = 0.0;
    double averageCallbackMs = 0.0;
    bool connected = false;
    QString lastError;
};
```

### `src/ProcessingTypes.h`

Use SI units at calculation boundaries. Store the user-facing values in readable units, then convert exactly once inside `AtmosphereCalculator`.

```cpp
#pragma once

#include "CameraTypes.h"

#include <QPointF>
#include <QString>
#include <QVector>

#include <cstdint>

struct OpticalConfig {
    double mainTelescopeApertureMm = 254.0;
    double subApertureDiameterMm = 60.0;
    double baselineSeparationMm = 150.0;
    double focalLengthMm = 2500.0;
    double wavelengthNm = 550.0;
    double pixelSizeUm = 5.86;
    double zenithAngleDeg = 0.0;
};

struct AcquisitionConfig {
    int frameWidth = 1920;
    int frameHeight = 1200;
    PixelFormat pixelFormat = PixelFormat::Mono8;
    double measurementRateHz = 100.0;
    double fullFramePreviewRateHz = 3.0;
    double roiPreviewRateHz = 20.0;
    double exposureTimeMs = 5.0;
    int targetSampleCount = 2000;
    double targetDurationSec = 20.0;
    bool enableHardwareAoi = true;
};

struct ProcessingConfig {
    int roiWidthPx = 64;
    int roiHeightPx = 64;
    int hardwareAoiMarginPx = 64;

    bool useOtsu = true;
    int otsuHistogramBins = 256;
    int otsuMinimumComponentAreaPx = 2;
    int otsuMaximumComponentAreaPx = 4096;
    int smallKernelRadiusPx = 4;
    int smallKernelHalfWidthPx = 1;
    double minimumPeakDistancePx = 8.0;
    double minimumCentroidIntensity = 1.0;

    int roiRecenteringDistanceToEdgePx = 16;
    int roiRecenteringConsecutiveFrames = 5;
    int roiRecenteringCooldownMs = 3000;
    double roiRecenteringMinimumShiftPx = 8.0;
    int roiLostRelocalizationFrames = 10;

    int r0WindowFrames = 1000;
    double resultUpdateIntervalSec = 1.0;
    int tau0HistorySeconds = 3;
    double tau0MaximumLagMs = 200.0;
    int tau0MinimumSamples = 30;
};

struct TriggerConfig {
    TriggerMode mode = TriggerMode::Continuous;
    QString hardwareTriggerLine = QStringLiteral("Line1");
    bool triggerSelectorFrameStart = true;
    bool softwareTriggerEachFrame = false;
    bool allowPartialScan = false;
};

struct StorageConfig {
    QString outputDirectory;
    double resultRecordIntervalSec = 1.0;
    bool saveParameterCsv = true;
    bool saveCentroidCsv = true;
    bool saveDiagnosticsCsv = true;
    bool saveRunMetadataJson = true;
    bool imageSavingFixedOff = true;
};

struct UiConfig {
    bool showFullFramePreview = true;
    bool showRoiPreview = true;
    bool drawHardwareAoi = true;
    bool drawSoftwareRois = true;
};

struct CentroidMeasurement {
    QPointF centroidPx;
    double peakIntensity = 0.0;
    double integratedIntensity = 0.0;
    int componentAreaPx = 0;
    bool valid = false;
    QString diagnostic;
};

struct TwoStarMeasurement {
    CentroidMeasurement starA;
    CentroidMeasurement starB;
    RoiRect roiA;
    RoiRect roiB;
    QPointF fullFrameStarA;
    QPointF fullFrameStarB;
    bool validPair = false;
    QString diagnostic;
};

struct DifferentialSample {
    double timestampSec = 0.0;
    double longitudinalPx = 0.0;
    double transversePx = 0.0;
    double longitudinalArcsec = 0.0;
    double transverseArcsec = 0.0;
    bool valid = false;
};

struct AtmosphereResult {
    bool valid = false;
    bool underResolved = false;
    QString statusMessage;
    std::uint64_t windowEndSequence = 0;
    int validSampleCount = 0;
    double r0LongitudinalM = 0.0;
    double r0TransverseM = 0.0;
    double r0LineOfSightM = 0.0;
    double r0ZenithM = 0.0;
    double seeingArcsec = 0.0;
    double theta0Arcsec = 0.0;
    double tau0Ms = 0.0;
    double measuredRateHz = 0.0;
    double longitudinalVarianceArcsec2 = 0.0;
    double transverseVarianceArcsec2 = 0.0;
};

struct MeasurementResult {
    std::uint64_t sequence = 0;
    double timestampSec = 0.0;
    TwoStarMeasurement stars;
    DifferentialSample differential;
    AtmosphereResult atmosphere;
};

struct RoiOverlay {
    RoiRect hardwareAoi;
    RoiRect roiA;
    RoiRect roiB;
    QPointF starA;
    QPointF starB;
    bool hasHardwareAoi = false;
    bool hasRois = false;
};
```

Do not add prism baseline, dark-field, hot-pixel, simulation, or camera-specific conversion fields to these contracts.

## Task 3 — Implement one configuration owner and persistence

### `src/AppConfig.h`

```cpp
#pragma once

#include "ProcessingTypes.h"

#include <QStringList>

class QSettings;

struct AppConfig {
    OpticalConfig optical;
    AcquisitionConfig acquisition;
    ProcessingConfig processing;
    TriggerConfig trigger;
    StorageConfig storage;
    UiConfig ui;

    static AppConfig defaults();
    QStringList validate() const;
    void load(QSettings &settings);
    void save(QSettings &settings) const;
};
```

### `src/AppConfig.cpp` requirements

Implement `defaults()` by returning the values in this plan. `validate()` must return human-readable messages instead of silently clamping values. The Settings dialog refuses Apply/OK when the list is non-empty.

At minimum validate:

```cpp
QStringList AppConfig::validate() const
{
    QStringList errors;

    if (optical.mainTelescopeApertureMm <= 0.0)
        errors << QStringLiteral("主镜口径必须大于 0 mm");
    if (optical.subApertureDiameterMm <= 0.0)
        errors << QStringLiteral("子瞳直径必须大于 0 mm");
    if (optical.baselineSeparationMm <= 0.0)
        errors << QStringLiteral("窗口中心距离必须大于 0 mm");
    if (optical.focalLengthMm <= 0.0)
        errors << QStringLiteral("望远镜焦距必须大于 0 mm");
    if (optical.wavelengthNm <= 0.0)
        errors << QStringLiteral("波长必须大于 0 nm");
    if (optical.pixelSizeUm <= 0.0)
        errors << QStringLiteral("像元尺寸必须大于 0 um");

    if (acquisition.frameWidth != 1920 || acquisition.frameHeight != 1200)
        errors << QStringLiteral("第一版仅允许 1920 x 1200 全画幅");
    if (acquisition.pixelFormat != PixelFormat::Mono8)
        errors << QStringLiteral("第一版仅允许 Mono8");
    if (acquisition.measurementRateHz < 100.0)
        errors << QStringLiteral("测量采样率必须至少为 100 Hz");
    if (acquisition.fullFramePreviewRateHz <= 0.0 ||
        acquisition.roiPreviewRateHz <= 0.0)
        errors << QStringLiteral("预览刷新率必须大于 0 Hz");
    if (acquisition.exposureTimeMs <= 0.0 || acquisition.exposureTimeMs > 10.0)
        errors << QStringLiteral("单次曝光时间必须在 (0, 10] ms 内");
    if (acquisition.targetSampleCount <= 0 || acquisition.targetDurationSec <= 0.0)
        errors << QStringLiteral("目标采样数和目标时长必须大于 0");

    if (processing.roiWidthPx < 16 || processing.roiHeightPx < 16)
        errors << QStringLiteral("软件 ROI 至少为 16 x 16 像素");
    if (processing.otsuHistogramBins < 256 || processing.otsuHistogramBins > 16384)
        errors << QStringLiteral("Otsu 直方图 bin 数必须在 256 到 16384 之间");
    if (processing.otsuMinimumComponentAreaPx < 1 ||
        processing.otsuMaximumComponentAreaPx < processing.otsuMinimumComponentAreaPx)
        errors << QStringLiteral("连通域面积范围无效");
    if (processing.smallKernelRadiusPx < 1 || processing.smallKernelRadiusPx > 20)
        errors << QStringLiteral("小核半径必须在 1 到 20 像素之间");
    if (processing.r0WindowFrames < 2)
        errors << QStringLiteral("r0 计算窗口至少需要 2 帧");
    if (processing.resultUpdateIntervalSec <= 0.0)
        errors << QStringLiteral("结果更新时间隔必须大于 0 秒");

    if (storage.outputDirectory.trimmed().isEmpty())
        errors << QStringLiteral("数据输出目录不能为空");
    if (!storage.imageSavingFixedOff)
        errors << QStringLiteral("第一版禁止保存原始图像");

    return errors;
}
```

Use explicit keys so settings remain readable and compatible with later versions:

```cpp
settings.beginGroup(QStringLiteral("physical"));
settings.setValue(QStringLiteral("mainTelescopeApertureMm"), optical.mainTelescopeApertureMm);
settings.setValue(QStringLiteral("subApertureDiameterMm"), optical.subApertureDiameterMm);
settings.setValue(QStringLiteral("baselineSeparationMm"), optical.baselineSeparationMm);
settings.setValue(QStringLiteral("focalLengthMm"), optical.focalLengthMm);
settings.setValue(QStringLiteral("wavelengthNm"), optical.wavelengthNm);
settings.setValue(QStringLiteral("pixelSizeUm"), optical.pixelSizeUm);
settings.endGroup();

settings.beginGroup(QStringLiteral("acquisition"));
settings.setValue(QStringLiteral("frameWidth"), acquisition.frameWidth);
settings.setValue(QStringLiteral("frameHeight"), acquisition.frameHeight);
settings.setValue(QStringLiteral("measurementRateHz"), acquisition.measurementRateHz);
settings.setValue(QStringLiteral("fullFramePreviewRateHz"), acquisition.fullFramePreviewRateHz);
settings.setValue(QStringLiteral("roiPreviewRateHz"), acquisition.roiPreviewRateHz);
settings.setValue(QStringLiteral("exposureTimeMs"), acquisition.exposureTimeMs);
settings.setValue(QStringLiteral("targetSampleCount"), acquisition.targetSampleCount);
settings.setValue(QStringLiteral("targetDurationSec"), acquisition.targetDurationSec);
settings.setValue(QStringLiteral("enableHardwareAoi"), acquisition.enableHardwareAoi);
settings.endGroup();

settings.beginGroup(QStringLiteral("processing"));
settings.setValue(QStringLiteral("roiWidthPx"), processing.roiWidthPx);
settings.setValue(QStringLiteral("roiHeightPx"), processing.roiHeightPx);
settings.setValue(QStringLiteral("hardwareAoiMarginPx"), processing.hardwareAoiMarginPx);
settings.setValue(QStringLiteral("otsuMinimumComponentAreaPx"), processing.otsuMinimumComponentAreaPx);
settings.setValue(QStringLiteral("otsuMaximumComponentAreaPx"), processing.otsuMaximumComponentAreaPx);
settings.setValue(QStringLiteral("smallKernelRadiusPx"), processing.smallKernelRadiusPx);
settings.setValue(QStringLiteral("minimumPeakDistancePx"), processing.minimumPeakDistancePx);
settings.setValue(QStringLiteral("roiRecenteringDistanceToEdgePx"), processing.roiRecenteringDistanceToEdgePx);
settings.setValue(QStringLiteral("roiRecenteringConsecutiveFrames"), processing.roiRecenteringConsecutiveFrames);
settings.setValue(QStringLiteral("roiRecenteringCooldownMs"), processing.roiRecenteringCooldownMs);
settings.setValue(QStringLiteral("roiRecenteringMinimumShiftPx"), processing.roiRecenteringMinimumShiftPx);
settings.setValue(QStringLiteral("roiLostRelocalizationFrames"), processing.roiLostRelocalizationFrames);
settings.setValue(QStringLiteral("r0WindowFrames"), processing.r0WindowFrames);
settings.setValue(QStringLiteral("resultUpdateIntervalSec"), processing.resultUpdateIntervalSec);
settings.setValue(QStringLiteral("tau0HistorySeconds"), processing.tau0HistorySeconds);
settings.setValue(QStringLiteral("tau0MaximumLagMs"), processing.tau0MaximumLagMs);
settings.setValue(QStringLiteral("tau0MinimumSamples"), processing.tau0MinimumSamples);
settings.endGroup();
```

Persist trigger, storage, and UI fields in analogous `trigger`, `storage`, and `ui` groups. `load()` must read with the defaults as fallbacks, then the caller must validate. Do not use a singleton; pass an `AppConfig` snapshot into workers and send a new validated snapshot through a queued reconfiguration slot.

## Task 4 — Separate acquisition, measurement, and UI data flow

The most important concurrency rule is that the camera callback never calls Qt widgets, never runs Otsu/connected components, and never writes files. The UI never waits on the camera and never drains a high-rate queue.

### `src/FrameQueue.h/.cpp`

Implement a bounded FIFO for measurement frames. The queue must have a fixed capacity of `8`; when full, discard the oldest frame and increment an atomic drop counter. Never allow unbounded growth.

```cpp
#pragma once

#include "CameraTypes.h"

#include <QMutex>
#include <QWaitCondition>

#include <atomic>
#include <deque>

class FrameQueue final {
public:
    explicit FrameQueue(std::size_t capacity = 8);

    void push(CameraFrame frame);
    bool tryPopOldest(CameraFrame &frame);
    bool tryPopLatest(CameraFrame &frame);
    void close();
    void clear();
    std::size_t size() const;
    std::uint64_t droppedCount() const;

private:
    const std::size_t capacity_;
    mutable QMutex mutex_;
    std::deque<CameraFrame> frames_;
    bool closed_ = false;
    std::atomic<std::uint64_t> dropped_{0};
};
```

`push()` must reject frames after `close()`. `tryPopLatest()` returns the newest frame and clears older queued frames because measurement must not process stale images. Use `QMutexLocker`; do not hold the lock while doing OpenCV work.

### `src/DisplayMailbox.h/.cpp`

Use a latest-only mailbox for display. Each update replaces the previous snapshot; the GUI timer takes a clone or an immutable snapshot and immediately releases the lock.

```cpp
#pragma once

#include "CameraTypes.h"
#include "ProcessingTypes.h"

#include <QMutex>

struct DisplaySnapshot {
    cv::Mat fullFrameMono8;
    cv::Mat roiAMono8;
    cv::Mat roiBMono8;
    RoiOverlay overlay;
    CameraStatistics cameraStats;
    MeasurementResult latestResult;
    std::uint64_t sequence = 0;
};

class DisplayMailbox final {
public:
    void publish(DisplaySnapshot snapshot);
    bool tryTake(DisplaySnapshot &snapshot) const;

private:
    mutable QMutex mutex_;
    DisplaySnapshot snapshot_;
    bool hasSnapshot_ = false;
};
```

`publish()` may receive a `cv::Mat` that belongs to a worker, so clone the matrices before publishing or guarantee ownership through a deep-copy helper. The GUI timer must never hold a reference to a worker-owned buffer.

### Required thread ownership table

| Object | Thread | Must not do |
|---|---|---|
| `PylonCamera` | camera worker thread | UI calls, OpenCV segmentation, file writes |
| `CameraWorker` | camera worker thread | QWidget access, synchronous GUI waits |
| `MeasurementWorker` | measurement worker thread | QWidget access, pylon node access |
| `ResultWriter` | measurement worker or dedicated writer thread | image saving |
| `MainWindow`/`SettingsDialog` | GUI thread | per-frame algorithm, camera callback work |
| GUI timers | GUI thread | blocking on a queue or camera |

`QThread` objects are owned by `MainWindow`, but workers are moved to the corresponding threads. Connect all worker control slots with `Qt::QueuedConnection`. Do not create a `QThread` inside a worker or subclass `QThread` for business logic.

## Task 5 — Implement Basler pylon camera ownership and acquisition

### `src/PylonCamera.h/.cpp`

This is the only module allowed to include pylon headers and use pylon node maps. The remainder of the program sees only `CameraFrame`, `CameraCapabilities`, and `CameraStatistics`.

Required public operations:

```cpp
#pragma once

#include "CameraTypes.h"

#include <QObject>

#include <memory>

class PylonCamera final {
public:
    PylonCamera();
    ~PylonCamera();

    bool openFirstCompatibleCamera(QString *error);
    void close();
    bool isOpen() const;

    CameraCapabilities capabilities() const;
    CameraStatistics statistics() const;

    bool configureMono8(QString *error);
    bool configureTrigger(const TriggerConfig &config, QString *error);
    bool configureHardwareAoi(const RoiRect &aoi, QString *error);
    bool resetToFullFrame(QString *error);

    bool startGrabbing(QString *error);
    void stopGrabbing();
    bool executeSoftwareTrigger(QString *error);

    // The callback must only copy a Mono8 buffer and metadata into CameraFrame.
    void setFrameCallback(std::function<void(CameraFrame)> callback);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
```

Inside the implementation:

1. Call `PylonInitialize()` exactly once for the application lifetime and `PylonTerminate()` after all camera objects have been destroyed. A small RAII guard in `PylonCamera.cpp` is acceptable.
2. Enumerate devices, select the first device whose model is `acA1920-40gm` or whose reported capabilities match Mono8 plus the required AOI/trigger features, and report model/serial in `CameraCapabilities`.
3. Read the camera nodes for sensor size and AOI increments. Record the reported default size as `1920 x 1200`; do not assume that the sensor maximum `1936 x 1216` is the acquisition size.
4. Configure `PixelFormat` to `Mono8`. If the node cannot be set to Mono8, fail with a clear error; do not fall back to Mono16.
5. Configure the exposure time from `AcquisitionConfig`; enforce the validation limit of `<= 10 ms`.
6. Apply the hardware AOI as one rectangle that encloses both software ROIs. AOI x/y/width/height must be rounded to the camera's reported increments and clamped to the sensor bounds.
7. Use the pylon SDK's official grab-result lifetime pattern. Convert the grab buffer to a copied `cv::Mat(CV_8UC1)`. Never publish a view whose backing buffer is about to be released by pylon.

Use the following node behavior as the implementation contract; adapt names only to the installed pylon API version:

```cpp
// Mono8 only.
pixelFormatNode->SetValue("Mono8");

// Continuous/free-run mode.
triggerSelectorNode->SetValue("FrameStart");
triggerModeNode->SetValue("Off");

// Software-trigger mode.
triggerSelectorNode->SetValue("FrameStart");
triggerModeNode->SetValue("On");
triggerSourceNode->SetValue("Software");

// Hardware-trigger mode.
triggerSelectorNode->SetValue("FrameStart");
triggerModeNode->SetValue("On");
triggerSourceNode->SetValue(config.hardwareTriggerLine.toStdString().c_str());
```

Do not combine `Software` trigger with an unbounded loop in the GUI. If `softwareTriggerEachFrame` is enabled, the camera worker issues the trigger from its own acquisition loop at the configured measurement cadence; the GUI only requests start/stop.

### `src/CameraWorker.h/.cpp`

```cpp
#pragma once

#include "AppConfig.h"
#include "DisplayMailbox.h"
#include "FrameQueue.h"
#include "PylonCamera.h"

#include <QObject>

class CameraWorker final : public QObject {
    Q_OBJECT
public:
    explicit CameraWorker(FrameQueue &measurementQueue,
                          DisplayMailbox &displayMailbox,
                          QObject *parent = nullptr);

public slots:
    void configure(AppConfig config);
    void start();
    void stop();
    void applyHardwareAoi(RoiRect aoi, std::uint64_t generation);
    void requestFullFrame();
    void executeSoftwareTrigger();

signals:
    void cameraReady(CameraCapabilities capabilities);
    void cameraError(QString message);
    void cameraStatsUpdated(CameraStatistics stats);
    void frameReceived(std::uint64_t sequence);
    void hardwareAoiApplied(RoiRect aoi, std::uint64_t generation);

private:
    void onFrame(CameraFrame frame);

    FrameQueue &measurementQueue_;
    DisplayMailbox &displayMailbox_;
    PylonCamera camera_;
    AppConfig config_;
    std::atomic_bool running_{false};
};
```

`onFrame()` does only: validate Mono8, update statistics, push the frame to `FrameQueue`, and publish a display copy at a throttled cadence. The full-frame display throttle is approximately `3 Hz`; the ROI display throttle is approximately `20 Hz`. The measurement queue receives every available valid frame and is not throttled to a preview rate.

Default AOI startup sequence:

1. Open camera and configure `Mono8`.
2. Reset hardware AOI to `(0, 0, 1920, 1200)` aligned to camera increments.
3. Start grabbing.
4. Let `MeasurementWorker` perform initial full-frame locating.
5. Apply one enclosing hardware AOI only after both software ROIs are known.

When applying an AOI, use stop → set width/height/offset → restart, unless the installed camera explicitly supports changing AOI while grabbing. Increment `configurationGeneration` and notify the measurement worker so frames from the old AOI cannot be interpreted using the new ROI coordinates.

## Task 6 — Implement the reference centroid and two-star ROI state machine

### `src/CentroidEngine.h/.cpp`

The implementation must be the requested reference method: Otsu thresholding, 4-connected components, then a small-kernel intensity-weighted centroid. Do not copy DIMM's old `IntensityCog`, `GaussianFit`, dark-field, or hot-pixel options.

```cpp
#pragma once

#include "ProcessingTypes.h"

#include <opencv2/core.hpp>

class CentroidEngine final {
public:
    explicit CentroidEngine(ProcessingConfig config);

    void setConfig(ProcessingConfig config);
    CentroidMeasurement locate(const cv::Mat &roiMono8,
                               const QPointF *expectedLocalCentroid = nullptr) const;

private:
    ProcessingConfig config_;
};
```

Required algorithm order:

```cpp
CV_Assert(roiMono8.type() == CV_8UC1);

cv::Mat binary;
double otsuThreshold = cv::threshold(
    roiMono8, binary, 0, 255, cv::THRESH_BINARY | cv::THRESH_OTSU);

cv::Mat labels;
cv::Mat stats;
cv::Mat centroids;
const int componentCount = cv::connectedComponentsWithStats(
    binary, labels, stats, centroids, 4, CV_32S);
```

Filter components using the configurable minimum/maximum area. If an expected centroid exists, score candidates by distance to the expected location, then by integrated intensity/peak intensity. Without history, select the strongest valid candidate and emit a diagnostic that the tracker has just initialized.

For the selected component, calculate the final centroid from a small square kernel around the component centroid. The kernel must be clipped to the ROI and use the original Mono8 intensities after subtracting the local background floor:

```cpp
const int radius = config_.smallKernelRadiusPx;
const int cx = qBound(0, qRound(componentCenter.x), roiMono8.cols - 1);
const int cy = qBound(0, qRound(componentCenter.y), roiMono8.rows - 1);

double sumWeight = 0.0;
double sumX = 0.0;
double sumY = 0.0;
double peak = 0.0;
for (int y = std::max(0, cy - radius); y <= std::min(roiMono8.rows - 1, cy + radius); ++y) {
    for (int x = std::max(0, cx - radius); x <= std::min(roiMono8.cols - 1, cx + radius); ++x) {
        const double weight = std::max(0.0,
            static_cast<double>(roiMono8.at<std::uint8_t>(y, x)) - localBackground);
        sumWeight += weight;
        sumX += weight * static_cast<double>(x);
        sumY += weight * static_cast<double>(y);
        peak = std::max(peak, static_cast<double>(roiMono8.at<std::uint8_t>(y, x)));
    }
}

if (sumWeight <= config_.minimumCentroidIntensity) {
    result.valid = false;
    result.diagnostic = QStringLiteral("小核加权强度不足");
} else {
    result.centroidPx = QPointF(sumX / sumWeight, sumY / sumWeight);
    result.integratedIntensity = sumWeight;
    result.peakIntensity = peak;
    result.valid = true;
}
```

The local background may be the minimum/low percentile of the kernel or the component bounding-box border; document the selected deterministic method in code. Do not subtract a dark-field template.

### `src/TwoStarTracker.h/.cpp`

```cpp
#pragma once

#include "CentroidEngine.h"

#include <QObject>

struct TrackerUpdate {
    TwoStarMeasurement measurement;
    RoiOverlay overlay;
    bool requestHardwareAoi = false;
    RoiRect requestedHardwareAoi;
    std::uint64_t requestGeneration = 0;
    bool requestFullFrameRelocalization = false;
};

class TwoStarTracker final {
public:
    explicit TwoStarTracker(AppConfig config);

    void reset();
    TrackerUpdate locateFromFullFrame(const CameraFrame &frame);
    TrackerUpdate processAoiFrame(const CameraFrame &frame);
    void applyHardwareAoi(RoiRect aoi, std::uint64_t generation);

    RoiRect hardwareAoi() const;
    RoiRect roiA() const;
    RoiRect roiB() const;
    bool initialized() const;

private:
    bool pairCandidates(const std::vector<QPointF> &candidates,
                        QPointF &starA, QPointF &starB) const;
    RoiRect makeEnclosingAoi(QPointF starA, QPointF starB) const;
    void updateRecenteringState(const TwoStarMeasurement &measurement,
                                TrackerUpdate &update);

    AppConfig config_;
    CentroidEngine centroidEngine_;
    RoiRect hardwareAoi_;
    RoiRect roiA_;
    RoiRect roiB_;
    QPointF lastStarA_;
    QPointF lastStarB_;
    bool initialized_ = false;
    int nearEdgeConsecutiveA_ = 0;
    int nearEdgeConsecutiveB_ = 0;
    int lostPairFrames_ = 0;
    qint64 lastAoiChangeMs_ = 0;
    std::uint64_t aoiGeneration_ = 0;
};
```

Full-frame locating must:

1. Run the same Otsu/component candidate extraction over `1920 x 1200`.
2. Reject components outside the configured area range.
3. Pair two candidates using minimum separation/brightness/shape consistency and reject a pair that is too close to be two stars. The exact score must be deterministic and logged.
4. Assign stable identities: choose left-to-right in image x on first initialization; afterwards assign the candidate nearest the previous StarA to StarA and the other to StarB. Never swap identities merely because the stars move.
5. Create `roiA` and `roiB` centered on the full-frame star positions, each exactly `roiWidthPx x roiHeightPx` and `roiHeightPx`, clipped to the full-frame bounds.
6. Set `hardwareAoi` to the bounding rectangle of both software ROIs plus `hardwareAoiMarginPx`, then align it to camera AOI increments in `PylonCamera`.

Coordinate conversion is mandatory after the hardware AOI is active:

```cpp
const int localX = roi.x - frame.sourceRect.x;
const int localY = roi.y - frame.sourceRect.y;
const cv::Rect localRect(localX, localY, roi.width, roi.height);
if ((localRect & cv::Rect(0, 0, frame.mono8.cols, frame.mono8.rows)) != localRect) {
    // The ROI is no longer inside the current AOI: invalidate this frame and relocalize.
}
```

ROI re-centering must follow the DIMM-style state machine:

- If a valid centroid is within `roiRecenteringDistanceToEdgePx` of any ROI edge for `roiRecenteringConsecutiveFrames` consecutive frames, request a new centered ROI.
- The new ROI center must move at least `roiRecenteringMinimumShiftPx`.
- Do not issue another AOI request during `roiRecenteringCooldownMs`.
- If either star is lost for `roiLostRelocalizationFrames`, invalidate the pair and request full-frame relocalization.
- During AOI reconfiguration or relocalization, do not append a differential sample.
- Move one hardware AOI containing both ROIs; never configure two hardware AOIs and never move hardware AOI for every normal frame.

## Task 7 — Port DIMM-compatible formulas into one calculator

### `src/AtmosphereCalculator.h/.cpp`

No formula may live in `MainWindow`, `MeasurementWorker`, or the settings dialog.

```cpp
#pragma once

#include "ProcessingTypes.h"

#include <deque>

class AtmosphereCalculator final {
public:
    explicit AtmosphereCalculator(AppConfig config);

    void setConfig(AppConfig config);
    void reset();
    void append(DifferentialSample sample);
    AtmosphereResult calculate(std::uint64_t windowEndSequence,
                               double measuredRateHz) const;
    int validSampleCount() const;

private:
    double calculateTau0(bool &underResolved) const;
    AppConfig config_;
    std::deque<DifferentialSample> window_;
    std::deque<DifferentialSample> tauHistory_;
};
```

Implement these exact DIMM operations, with explicit unit conversion:

```cpp
const double lambdaM = config_.optical.wavelengthNm * 1.0e-9;
const double diameterM = config_.optical.subApertureDiameterMm * 1.0e-3;
const double baselineM = config_.optical.baselineSeparationMm * 1.0e-3;
const double pixelScaleRad =
    (config_.optical.pixelSizeUm * 1.0e-6) /
    (config_.optical.focalLengthMm * 1.0e-3);
const double arcsecPerPixel = pixelScaleRad * 206265.0;

double dx = starB.x() - starA.x();
double dy = starB.y() - starA.y();
double longitudinalPx = dx * std::cos(angleRad) + dy * std::sin(angleRad);
double transversePx = -dx * std::sin(angleRad) + dy * std::cos(angleRad);
```

`angleRad` is the configured/reference image-axis rotation used by the DIMM implementation. For the first KY-DIMM version default it to `0` unless the existing DIMM UI already exposes a calibrated image rotation. Do not infer a prism angle and do not use `150 mm` as an image-space separation.

Use population variance exactly as in the DIMM code: denominator `N`, not `N - 1`.

```cpp
double populationVariance(const std::vector<double> &values)
{
    if (values.empty())
        return 0.0;
    const double mean = std::accumulate(values.begin(), values.end(), 0.0) /
                        static_cast<double>(values.size());
    double sum = 0.0;
    for (double value : values)
        sum += (value - mean) * (value - mean);
    return sum / static_cast<double>(values.size());
}
```

The DIMM coefficients and outputs must be implemented as follows:

```cpp
const double coefficientLongitudinal = 2.0 * lambdaM * lambdaM *
    (0.179 * std::pow(diameterM, -1.0 / 3.0) -
     0.0968 * std::pow(baselineM, -1.0 / 3.0));
const double coefficientTransverse = 2.0 * lambdaM * lambdaM *
    (0.179 * std::pow(diameterM, -1.0 / 3.0) -
     0.145 * std::pow(baselineM, -1.0 / 3.0));

const double r0Longitudinal = std::pow(
    coefficientLongitudinal / longitudinalVarianceRad2, 3.0 / 5.0);
const double r0Transverse = std::pow(
    coefficientTransverse / transverseVarianceRad2, 3.0 / 5.0);
const double r0LineOfSight = 0.5 * (r0Longitudinal + r0Transverse);

const double zenithRad = config_.optical.zenithAngleDeg * M_PI / 180.0;
const double r0Zenith = r0LineOfSight * std::pow(std::cos(zenithRad), -3.0 / 5.0);
const double seeingArcsec = 0.98 * lambdaM / r0Zenith * 206265.0;
```

For `theta0`, preserve the DIMM project's expression and input units:

```cpp
const double theta0Arcsec = 0.64 *
    (4.0 / std::pow(longitudinalVarianceArcsec2, 0.65)) *
    std::pow(std::cos(zenithRad), 8.0 / 5.0);
```

If the reference code uses a different calibrated `angleRad`, copy that variable's source and conversion exactly and document it. Do not change the formula to use the telescope baseline as an image-coordinate distance.

### Frame-count window and update schedule

- Keep only the latest `processing.r0WindowFrames` valid paired samples.
- Before the window is full, return `valid=false`, `statusMessage="等待足够有效帧"`.
- The first valid result is emitted immediately after the `1000`th valid paired frame by default.
- After warm-up, update only when `validFrameCountSinceLastResult >= round(measurementRateHz * resultUpdateIntervalSec)`; at `100 Hz` and `1 s`, this is every `100` frames.
- If the measured rate is below `100 Hz`, return `valid=false`, `statusMessage="实际测量率低于 100 Hz"`, and do not expose stale values as current values.

### tau0

Maintain the latest `tauHistorySeconds` (default `3 s`) of valid differential samples. Search lags up to `tau0MaximumLagMs` (default `200 ms`) and require at least `tau0MinimumSamples` (default `30`). Calculate the normalized autocorrelation for longitudinal and transverse series, find the first crossing of `1/e`, and linearly interpolate between the two adjacent lag points. If the crossing is not resolved in either series, mark `underResolved=true`; average the two valid crossings when both exist. If there are not enough samples, return `tau0Ms=0` and an explanatory status.

## Task 8 — Implement the measurement-worker pipeline

### `src/MeasurementWorker.h/.cpp`

```cpp
#pragma once

#include "AppConfig.h"
#include "AtmosphereCalculator.h"
#include "DisplayMailbox.h"
#include "FrameQueue.h"
#include "TwoStarTracker.h"

#include <QObject>

class MeasurementWorker final : public QObject {
    Q_OBJECT
public:
    explicit MeasurementWorker(FrameQueue &queue,
                               DisplayMailbox &displayMailbox,
                               QObject *parent = nullptr);

public slots:
    void configure(AppConfig config);
    void start();
    void stop();
    void onHardwareAoiApplied(RoiRect aoi, std::uint64_t generation);

signals:
    void hardwareAoiRequested(RoiRect aoi, std::uint64_t generation);
    void resultReady(MeasurementResult result);
    void roiStateChanged(RoiOverlay overlay);
    void measurementStatsUpdated(double measuredRateHz, std::uint64_t validPairs);
    void measurementError(QString message);

private:
    void processOneFrame(const CameraFrame &frame);
    void publishDisplay(const CameraFrame &frame,
                        const TrackerUpdate &update,
                        const MeasurementResult &result);

    FrameQueue &queue_;
    DisplayMailbox &displayMailbox_;
    AppConfig config_;
    TwoStarTracker tracker_;
    AtmosphereCalculator calculator_;
    std::atomic_bool running_{false};
    bool initialLocateDone_ = false;
    bool aoiChangePending_ = false;
    std::uint64_t expectedAoiGeneration_ = 0;
    std::uint64_t validPairCount_ = 0;
    std::uint64_t validPairsSinceLastResult_ = 0;
};
```

The worker loop must process frames in this order:

```text
start requested
  -> camera has already been configured to Mono8 and 1920x1200
  -> consume full-frame images until two valid stars are found
  -> assign stable StarA/StarB identities
  -> create two 64x64 software ROIs in full-frame coordinates
  -> create one enclosing hardware AOI and emit hardwareAoiRequested
  -> wait for hardwareAoiApplied and discard old-generation frames
  -> crop ROI A and ROI B using sourceRect-aware coordinates
  -> Otsu + 4-connected components + small-kernel centroid for each ROI
  -> if both centroids valid, form one differential sample
  -> update ROI recenter state; if it requests AOI, stop appending samples
  -> append valid paired sample to the 1000-frame calculator window
  -> enforce measured-rate >= 100Hz
  -> emit first result at frame 1000, then every configured interval in frames
  -> publish latest result and overlays through queued signals/mailbox
stop requested
  -> stop processing, close queue only after camera has stopped, reset state
```

Important implementation details:

1. A full-frame `CameraFrame` has `sourceRect=(0,0,1920,1200)`. After hardware AOI, `sourceRect` is the AOI's full-frame location, and every software ROI remains expressed in full-frame coordinates.
2. Never crop with `cv::Rect(roi.x, roi.y, ...)` after AOI unless the ROI is first translated by `sourceRect.x/y`.
3. If only one star is valid, do not synthesize the other centroid and do not append a sample.
4. If pylon AOI is being changed, clear/ignore queued frames from the previous generation. The first frame of the new generation is used to verify containment before appending data.
5. Rate measurement must use camera-frame timestamps or a monotonic clock around valid paired frames, not GUI timer timing.
6. The `100 Hz` gate applies to the actual measurement stream, not the full-frame preview and not the ROI preview timer.
7. The worker can request the GUI to update status text through a signal, but it must never call `QLabel::setText()` directly.

## Task 9 — Store result records without saving images

### `src/ResultWriter.h/.cpp`

Create one run directory below `StorageConfig::outputDirectory`, for example `KY-DIMM_20260812_143000`. Save only derived data and metadata:

```text
<run-directory>/
  atmosphere_summary.csv
  centroid_details.csv
  acquisition_diagnostics.csv
  run_metadata.json
```

There must be no `images/`, `full_frame/`, `roi_a/`, or `roi_b/` directory and no image file writer in the project.

```cpp
#pragma once

#include "AppConfig.h"
#include "CameraTypes.h"
#include "ProcessingTypes.h"

#include <QFile>
#include <QString>

class ResultWriter final {
public:
    ResultWriter() = default;
    ~ResultWriter();

    bool startRun(const AppConfig &config,
                  const CameraCapabilities &capabilities,
                  QString *error);
    void append(const MeasurementResult &result);
    void appendCameraStats(const CameraStatistics &stats);
    void finishRun();
    QString runDirectory() const;

private:
    QString runDirectory_;
    QFile atmosphereFile_;
    QFile centroidFile_;
    QFile diagnosticsFile_;
    bool running_ = false;
};
```

CSV headers must be written once. Minimum columns:

```text
atmosphere_summary.csv:
timestamp_s,window_end_sequence,valid_sample_count,measured_rate_hz,
r0_longitudinal_m,r0_transverse_m,r0_line_of_sight_m,r0_zenith_m,
seeing_arcsec,theta0_arcsec,tau0_ms,under_resolved,status

centroid_details.csv:
timestamp_s,sequence,star_a_x_px,star_a_y_px,star_b_x_px,star_b_y_px,
longitudinal_px,transverse_px,longitudinal_arcsec,transverse_arcsec,
roi_a_x,roi_a_y,roi_a_width,roi_a_height,roi_b_x,roi_b_y,
roi_b_width,roi_b_height,valid_pair,diagnostic

acquisition_diagnostics.csv:
timestamp_s,received_frames,dropped_frames,queue_dropped_frames,
measured_rate_hz,average_callback_ms,connected,last_error
```

`run_metadata.json` must include every physical, camera, processing, trigger, and storage parameter, including `imageSavingFixedOff=true`. Use `QSaveFile` or a temporary file plus rename for metadata. Flush CSV rows at the configured result interval; do not flush every camera frame if that creates an I/O bottleneck.

## Task 10 — Build the settings dialog with five functional pages

### `src/SettingsDialog.h/.cpp/.ui`

The tab titles must be exactly:

```text
物理/光学参数
采集参数
图像处理参数
触发设置
数据存储
```

Do not create a separate generic “系统参数” tab. The former DIMM settings can be used for visual reference, but only KY-DIMM fields belong here.

Expose these fields with stable object names:

```text
physicalMainApertureMmSpin
physicalSubApertureMmSpin
physicalBaselineMmSpin
physicalFocalLengthMmSpin
physicalWavelengthNmSpin
physicalPixelSizeUmSpin
physicalZenithAngleDegSpin

acquisitionWidthSpin
acquisitionHeightSpin
acquisitionPixelFormatCombo
acquisitionMeasurementRateSpin
acquisitionFullFramePreviewRateSpin
acquisitionRoiPreviewRateSpin
acquisitionExposureMsSpin
acquisitionTargetSamplesSpin
acquisitionTargetDurationSecSpin
acquisitionHardwareAoiCheck

processingRoiWidthSpin
processingRoiHeightSpin
processingAoiMarginSpin
processingOtsuBinsSpin
processingMinComponentAreaSpin
processingMaxComponentAreaSpin
processingSmallKernelRadiusSpin
processingMinimumPeakDistanceSpin
processingEdgeDistanceSpin
processingRecenteringConsecutiveSpin
processingRecenteringCooldownMsSpin
processingMinimumShiftSpin
processingLostFramesSpin
processingR0WindowFramesSpin
processingResultUpdateIntervalSpin
processingTauHistorySecondsSpin
processingTauMaximumLagMsSpin
processingTauMinimumSamplesSpin

triggerModeCombo
triggerHardwareLineCombo
triggerFrameStartCheck
triggerSoftwareEachFrameCheck
triggerAllowPartialScanCheck

storageOutputDirectoryEdit
storageBrowseButton
storageRecordIntervalSpin
storageParameterCsvCheck
storageCentroidCsvCheck
storageDiagnosticsCsvCheck
storageMetadataJsonCheck
storageImageSavingFixedOffLabel
```

Page behavior:

1. Physical/optical page shows `254`, `60`, `150`, `2500`, `550`, and `5.86` as editable numeric values. The `150 mm` label must say “两个圆形窗口中心距离 (mm)”; it is not the software ROI distance.
2. Acquisition page shows `1920 x 1200` and `Mono8` as read-only in this version. Show preview rates separately from the measurement rate. Help text must state that measurement rate is a hard validity requirement while preview rates only affect GUI refresh.
3. Image-processing page contains Otsu/component/small-kernel parameters, ROI tracking thresholds, and the frame-count r0 window. Do not expose dark-field or hot-pixel controls.
4. Trigger page supports `连续采集`, `软件触发`, and `硬件触发`. Continuous means camera free-run; software trigger means the application issues one frame-start trigger per requested frame; hardware trigger uses the selected line. The partial-scan checkbox is optional and must not silently change the physical measurement path.
5. Storage page exposes the output directory and CSV/JSON switches. Show a permanent label: `第一版不保存全画幅图像和 ROI 原始图像`. Do not add an image-save checkbox that can turn image saving on; `imageSavingFixedOff` remains true.

Apply/OK sequence:

```cpp
void SettingsDialog::apply()
{
    const AppConfig candidate = readWidgets();
    const QStringList errors = candidate.validate();
    if (!errors.isEmpty()) {
        QMessageBox::warning(this, tr("参数无效"), errors.join('\n'));
        return;
    }

    config_ = candidate;
    emit configApplied(config_);
    acceptedConfig_ = true;
}
```

`Apply` writes `QSettings` and emits the validated snapshot. `OK` calls `apply()` and closes only if validation succeeds. `Cancel` restores the pre-dialog snapshot and emits nothing. When a running measurement would be affected, disable fields that require camera restart or show a restart-required message; never mutate the camera node map from the GUI thread.

## Task 11 — Implement the KY-DIMM main window and display adapters

### `src/MainWindow.h/.cpp/.ui`

Use the DIMM project's dark blue/cyan visual language, but make the layout specific to one camera and two spots. The main window must show the measurement state clearly even when the 100 Hz validity gate is not satisfied.

```cpp
#pragma once

#include "AppConfig.h"
#include "CameraWorker.h"
#include "DisplayMailbox.h"
#include "MeasurementWorker.h"
#include "ResultWriter.h"

#include <QMainWindow>
#include <QThread>

class SettingsDialog;

class MainWindow final : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    void onStartClicked();
    void onStopClicked();
    void onSettingsClicked();
    void refreshFullFramePreview();
    void refreshRoiPreview();
    void refreshStatusSnapshot();
    void onResultReady(MeasurementResult result);
    void onCameraError(QString message);

private:
    void setupThreads();
    void stopThreads();
    void updateControlsForState(bool running);
    void updateResultCards(const AtmosphereResult &result);

    AppConfig config_;
    FrameQueue measurementQueue_;
    DisplayMailbox displayMailbox_;
    QThread cameraThread_;
    QThread measurementThread_;
    CameraWorker *cameraWorker_ = nullptr;
    MeasurementWorker *measurementWorker_ = nullptr;
    ResultWriter resultWriter_;
    QTimer *fullFrameTimer_ = nullptr;
    QTimer *roiTimer_ = nullptr;
    QTimer *statusTimer_ = nullptr;
    bool running_ = false;
};
```

Build the central layout using `QSplitter`:

```text
┌────────────────────────────────────────────────────────────────────┐
│ KY-DIMM | 相机状态 | 实际测量率 | 采集状态 | 设置 | 开始采集 | 停止 │
├───────────────────────────────┬────────────────────────────────────┤
│                               │ 四参数结果卡                         │
│     全画幅预览 1920×1200      │ r0 / seeing / theta0 / tau0          │
│     StarA、StarB、AOI overlay │ 有效帧数 / 窗口进度 / 状态信息        │
│                               ├────────────────────────────────────┤
│                               │ ROI A 64×64 | ROI B 64×64             │
│                               │ 质心坐标 / 峰值 / 连通域面积           │
├───────────────────────────────┴────────────────────────────────────┤
│ 采集日志 | ROI跟踪状态 | AOI状态 | 丢帧数 | 队列丢帧 | 最后错误        │
└────────────────────────────────────────────────────────────────────┘
```

Required widget object names:

```text
fullFrameImageLabel
roiAImageLabel
roiBImageLabel
starAStatusLabel
starBStatusLabel
hardwareAoiStatusLabel
measurementRateLabel
validFrameCountLabel
windowProgressBar
r0ValueLabel
seeingValueLabel
theta0ValueLabel
tau0ValueLabel
measurementStateLabel
cameraStateLabel
queueDroppedLabel
settingsButton
startAcquisitionButton
stopAcquisitionButton
```

Timer rules:

- `fullFrameTimer_`: GUI thread, interval `1000 / fullFramePreviewRateHz`, latest-only mailbox snapshot.
- `roiTimer_`: GUI thread, interval `1000 / roiPreviewRateHz`, latest-only mailbox snapshot.
- `statusTimer_`: GUI thread, 250–500 ms, updates status labels from the latest snapshot.

The timers must never call `FrameQueue::tryPopOldest()`, because that would steal frames from measurement. They use `DisplayMailbox::tryTake()` only. The preview painter maps full-frame coordinates onto the `1920 x 1200` image label and draws `hardwareAoi`, `roiA`, `roiB`, StarA, and StarB. Label the display as `全画幅 1920×1200`; do not claim that it is the sensor maximum.

Start/stop lifecycle:

```text
Start button
  -> validate current AppConfig
  -> clear queue/mailbox and reset worker state
  -> queued CameraWorker::configure/start
  -> queued MeasurementWorker::configure/start
  -> enable Stop, disable Start/unsafe settings

Stop button
  -> queued MeasurementWorker::stop
  -> queued CameraWorker::stop
  -> finish ResultWriter after worker-stopped signals
  -> enable Start and settings
```

Do not call `QThread::wait()` on the GUI thread while camera callbacks may still be running. Coordinate shutdown with signals, then quit threads after workers report stopped. If a bounded wait is unavoidable during application destruction, it must be after camera grabbing has stopped and must not be used for normal button clicks.

### `src/ImageDisplayAdapter.h/.cpp`

Keep image-to-widget conversion out of workers:

```cpp
#pragma once

#include "CameraTypes.h"
#include "ProcessingTypes.h"

#include <QImage>

class ImageDisplayAdapter final {
public:
    static QImage toGrayImage(const cv::Mat &mono8);
    static QImage drawOverlay(const QImage &base,
                              const RoiOverlay &overlay,
                              const QRect &fullFrameRect);
};
```

The adapter receives a deep-copied Mono8 matrix, converts it to `QImage::Format_Grayscale8`, and paints overlays with `QPainter`. No camera or pylon dependency is allowed here.

### `src/UiTheme.h/.cpp`

Adapt the DIMM palette: dark navy background, slightly lighter panels, cyan active controls, readable white/blue-gray text, and high-contrast invalid/warning colors. Keep the result cards visually prominent. Do not introduce a second unrelated style system.

## Task 12 — Add non-build static and pure-function verification

These tests must not require Qt GUI startup, pylon hardware, a camera, or a compiled C++ binary. They verify the source and algorithm contract, not the later hardware test.

### `tests/test_plan_static.py`

```python
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def test_required_source_files_exist():
    required = [
        "CameraTypes.h", "ProcessingTypes.h", "AppConfig.cpp",
        "FrameQueue.cpp", "DisplayMailbox.cpp", "PylonCamera.cpp",
        "CameraWorker.cpp", "CentroidEngine.cpp", "TwoStarTracker.cpp",
        "AtmosphereCalculator.cpp", "MeasurementWorker.cpp",
        "ResultWriter.cpp", "SettingsDialog.cpp", "MainWindow.cpp",
    ]
    missing = [name for name in required if not (SRC / name).exists()]
    assert not missing, missing


def test_cmake_is_qt6_and_lists_kydimm():
    cmake = read(ROOT / "CMakeLists.txt")
    assert "project(KY_DIMM" in cmake
    assert "find_package(Qt6" in cmake
    assert "Qt5" not in cmake
    assert "MinGW" not in cmake
    assert "pylon" in cmake


def test_camera_boundary_isolated():
    for path in SRC.glob("*.cpp"):
        text = read(path)
        if path.name != "PylonCamera.cpp":
            assert "#include <pylon/" not in text
            assert "Pylon::" not in text


def test_forbidden_legacy_features_are_absent_from_source():
    text = "\n".join(read(path) for path in SRC.glob("*"))
    forbidden = [
        "saveFullFrameImages", "saveRoiImages", "darkField",
        "HotPixel", "GaussianFit", "IntensityCog", "Simulation",
    ]
    for token in forbidden:
        assert token not in text, token


def test_required_thread_separation_names_are_present():
    worker_text = read(SRC / "CameraWorker.h") + read(SRC / "MeasurementWorker.h")
    ui_text = read(SRC / "MainWindow.cpp")
    assert "FrameQueue" in worker_text
    assert "DisplayMailbox" in worker_text
    assert "QTimer" in ui_text
```

### `tests/test_roi_state_machine.py`

Test pure helper functions or a small Python model of the state machine. Cover:

1. Five consecutive near-edge frames trigger one re-center request.
2. Four near-edge frames do not trigger a request.
3. A shift smaller than `8 px` does not trigger a request.
4. Cooldown suppresses a second request for `3000 ms`.
5. Ten consecutive lost-pair frames request full-frame relocalization.
6. The enclosing hardware AOI contains both `64 x 64` software ROIs plus the configured margin.
7. A frame whose `sourceRect` does not contain a software ROI is rejected rather than cropped with wrong coordinates.

### `tests/test_physics_calculator.py`

Test the unit and formula contract with the fixed initial values:

```python
import math


def test_pixel_scale():
    pixel_scale_rad = (5.86e-6) / 2.5
    assert math.isclose(pixel_scale_rad, 2.344e-6, rel_tol=1e-12)


def test_population_variance_uses_n():
    values = [1.0, 2.0, 3.0]
    mean = sum(values) / len(values)
    variance = sum((x - mean) ** 2 for x in values) / len(values)
    assert math.isclose(variance, 2.0 / 3.0)


def test_dimm_coefficients_are_positive_for_defaults():
    lam = 550e-9
    diameter = 60e-3
    baseline = 150e-3
    longitudinal = 2 * lam**2 * (
        0.179 * diameter ** (-1 / 3) - 0.0968 * baseline ** (-1 / 3)
    )
    transverse = 2 * lam**2 * (
        0.179 * diameter ** (-1 / 3) - 0.145 * baseline ** (-1 / 3)
    )
    assert longitudinal > 0
    assert transverse > 0
```

Run only:

```powershell
python -m pytest tests -q
git diff --check
```

If this directory is not a Git worktree, skip the Git command and report that fact. Do not run a build as part of this task.

## Task 13 — Final static audit and handoff report

Before handing the implementation to the owner, inspect the diff and verify every item below.

### Build configuration audit

- `CMakeLists.txt` references Qt6, OpenCV, and the Basler pylon SDK under `E:/Softwoare/Basler pylon/Development`.
- The target is named `KY_DIMM`.
- The source list contains all new files.
- No Qt5/MinGW/old `UI_New` source remains.
- No configure or build command was run.

### Camera audit

- `PylonCamera.cpp` is the only source that touches pylon types.
- Mono8 is required and checked.
- Default acquisition is `1920 x 1200`.
- The full sensor maximum `1936 x 1216` is not presented as the default image.
- Hardware AOI is one enclosing rectangle, not two camera AOIs.
- Software trigger and hardware trigger are selectable, while continuous/free-run is the default.
- Camera callback does not run image processing or UI code.

### Image-processing audit

- Full-frame locating finds two spots before software ROIs are created.
- StarA/StarB identity is stable across motion.
- Both software ROIs are measured in camera pixels and default to `64 x 64`.
- ROI coordinates are translated by `sourceRect` after hardware AOI.
- ROI recentering uses the consecutive-frame, minimum-shift, cooldown, and lost-frame settings.
- The centroid path is Otsu → 4-connected components → small-kernel intensity-weighted centroid.
- No dark-field template, hot-pixel correction, Gaussian fit, old IntensityCog, or simulation path was migrated.

### Physics/result audit

- Defaults are `D=60 mm`, `B=150 mm`, `f=2500 mm`, `lambda=550 nm`, pixel size `5.86 um`.
- The four-parameter formulas and coordinate projection match `UI_2/src/ImageProcessor.cpp`.
- Population variance divides by `N`.
- The r0 window is frame-count based, default `1000` valid paired frames.
- The first result appears only after the window is full.
- At 100 Hz and 1 second update interval, the result updates every 100 valid frames.
- Actual measurement rate below 100 Hz invalidates the four parameters.
- tau0 uses the latest three seconds, the configured maximum lag, minimum sample count, and 1/e crossing behavior.

### UI/thread/storage audit

- Settings has exactly the five required pages.
- Physical, acquisition, image-processing, trigger, and storage fields are configurable where specified.
- Full-frame preview and ROI preview have separate GUI timers.
- UI timers consume latest-only display snapshots and never steal measurement frames.
- Camera and measurement workers are separate QThreads.
- No GUI code blocks on camera acquisition.
- Only CSV/JSON derived records are saved; raw images are impossible in this version.

### Required handoff report

The executing agent must finish with a short report containing:

```text
Modified files:
- ...

Added files:
- ...

Static tests run:
- command and result

Build status:
- Not built by instruction

Known local SDK/path assumptions:
- ...

Items requiring owner hardware verification:
- pylon device enumeration and actual node names
- actual Mono8 frame delivery at >=100 Hz
- hardware AOI increment behavior
- Polaris two-spot full-frame detection
- ROI tracking during mount motion
```

The owner will perform the first CMake configure/build and camera test separately. Do not claim runtime success based only on source inspection.
