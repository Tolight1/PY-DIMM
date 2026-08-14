# 中文界面文案修复 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 统一 KY-DIMM 主界面、设置窗口、运行时诊断、历史界面和项目说明中的中文文案，不改变程序逻辑、配置含义或数据格式。

**Architecture:** 继续使用现有 Qt Widgets 文案来源，不引入新的翻译层或文案资源文件。静态界面文本修改 `.ui` 文件，动态文本修改对应 C++ 文件；通过独立的静态文案契约测试保护术语、标点、单位和关键用户可见字符串。

**Tech Stack:** C++17、Qt6 Widgets、Qt Designer `.ui`、CMake、Python 3、pytest、UTF-8。

---

## 文件结构与职责

- Create: `tests/test_chinese_copy.py` — 检查关键中文文案、废弃表达、历史界面标题和文档编码。
- Modify: `src/MainWindow.cpp` — 主界面按钮、状态标签、预览占位、日志和弹窗。
- Modify: `src/SettingsDialog.ui` — 设置页和字段的静态中文文本。
- Modify: `src/SettingsDialog.cpp` — 设置页动态标题、触发模式、按钮文本和目录选择器标题。
- Modify: `src/AppConfig.cpp` — 参数校验错误文案。
- Modify: `src/AtmosphereCalculator.cpp` — 大气参数计算状态文案。
- Modify: `src/CentroidEngine.cpp` — 质心诊断文案。
- Modify: `src/MeasurementWorker.cpp` — 测量线程错误文案。
- Modify: `src/PylonCamera.cpp` — 相机能力、配置和采集错误文案。
- Modify: `src/ResultWriter.cpp` — 结果目录和文件写入错误文案。
- Modify: `src/TwoStarTracker.cpp` — 双星定位和 ROI 诊断文案。
- Modify: `src/UI_New.ui` — 历史遗留窗口的窗口标题。
- Modify: `KY-DIMM-项目介绍.md` — UTF-8 中文说明、术语、标点、单位和 Markdown 链接文字。
- Do not modify: `src/*.h`、算法控制流、信号槽名、控件 `objectName`、CSV/JSON 字段名、`docs/superpowers/plans` 中的历史计划，以及上层仓库其他项目的已有变更。

## 文案基线

实现中始终遵守以下固定规则：

- 状态字段使用全角冒号：`相机：未连接`、`采集状态：已停止`、`实际测量率：-- Hz`。
- `ROI`、`AOI` 与中文之间留一个空格：`ROI 跟踪`、`硬件 AOI`。
- 统一使用“跟踪”和“重定位”，不使用“追踪”和“重定中心”。
- 数值和单位留一个空格：`5.86 µm`、`100 Hz`、`1 ms`；尺寸使用两侧有空格的 `×`：`1920 × 1200`。
- `r0`、`seeing`、`theta0`、`tau0`、`Mono8`、`Otsu`、`StarA`、`StarB`、`Line1` 等技术标识不翻译。
- “有效帧”只表示相机帧计数；统计窗口中的计数使用“有效样本”。
- 不改变任何字符串所在的信号、字段、文件名或数据结构，只改变给用户看的文本。

### Task 1: Add the copy contract tests

**Files:**
- Create: `tests/test_chinese_copy.py`

- [ ] **Step 1: Add failing tests for the approved copy contract**

Create `tests/test_chinese_copy.py` with this content:

```python
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"


def read_source(name: str) -> str:
    return (SRC / name).read_text(encoding="utf-8")


def test_main_window_uses_consistent_copy():
    text = read_source("MainWindow.cpp")
    expected = [
        "相机：未连接",
        "采集状态：已停止",
        "断开相机",
        "停止采集",
        "全画幅 1920 × 1200",
        "ROI A 64 × 64",
        "ROI 跟踪：未开始",
        "相机丢帧：%1",
        "队列丢帧：%1",
    ]
    forbidden = [
        "相机: 未连接",
        "断开连接",
        'tr("停止")',
        "ROI跟踪",
        "有效帧:",
    ]
    for phrase in expected:
        assert phrase in text, phrase
    for phrase in forbidden:
        assert phrase not in text, phrase


def test_settings_dialog_uses_consistent_copy():
    ui = (SRC / "SettingsDialog.ui").read_text(encoding="utf-8")
    source = read_source("SettingsDialog.cpp")
    expected_ui = [
        "主镜口径（mm）",
        "两个圆形窗口中心距离（mm）",
        "默认值：D = 60 mm、B = 150 mm（物理基线）、f = 2500 mm、λ = 550 nm、像元尺寸 = 5.86 µm。",
        "处理路径固定为 Otsu → 4 连通域 → 小核强度加权质心。",
        "软件触发模式下，每帧发送一次触发信号",
        "浏览…",
    ]
    expected_source = [
        'tr("ROI 重定位")',
        'tr("选择数据输出目录")',
        'tr("确定")',
        'tr("取消")',
    ]
    forbidden = [
        "主镜口径 (mm)",
        "重定中心",
        "浏览...",
        "一帧帧起始",
    ]
    for phrase in expected_ui:
        assert phrase in ui, phrase
    for phrase in expected_source:
        assert phrase in source, phrase
    for phrase in forbidden:
        assert phrase not in ui and phrase not in source, phrase


def test_runtime_copy_uses_consistent_terms():
    app_config = read_source("AppConfig.cpp")
    atmosphere = read_source("AtmosphereCalculator.cpp")
    tracker = read_source("TwoStarTracker.cpp")
    expected = [
        "子孔径直径必须大于 0 mm",
        "像元尺寸必须大于 0 µm",
        "当前版本仅允许 1920 × 1200 全画幅",
        "软件 ROI 至少为 16 × 16 像素",
        "等待足够的有效样本（%1 / %2）",
        "经过亮度/形状一致性过滤后，候选目标不足",
        "未找到满足最小间距要求的双星对",
    ]
    forbidden = [
        "子瞳直径必须大于 0 mm",
        "像元尺寸必须大于 0 um",
        "1920 x 1200",
        "16 x 16",
        "等待足够有效帧",
        "重定中心",
    ]
    combined = app_config + atmosphere + tracker
    for phrase in expected:
        assert phrase in combined, phrase
    for phrase in forbidden:
        assert phrase not in combined, phrase


def test_legacy_window_and_project_document_are_clean_utf8():
    legacy_ui = (SRC / "UI_New.ui").read_text(encoding="utf-8")
    document_path = ROOT / "KY-DIMM-项目介绍.md"
    document_bytes = document_path.read_bytes()
    document = document_bytes.decode("utf-8")

    assert "<string>KY-DIMM</string>" in legacy_ui
    assert "# KY-DIMM 项目介绍" in document
    assert "窗口中心距离" in document
    assert "ROI 跟踪" in document
    assert "µm" in document
    assert "椤圭洰浠嬬粛" not in document
    assert "ROI跟踪" not in document
    assert "重定中心" not in document
```

- [ ] **Step 2: Run the new tests before implementation**

Run:

```powershell
python -m pytest tests/test_chinese_copy.py -q
```

Expected: FAIL because the current source still contains `相机: 未连接`、`ROI跟踪`、旧的单位格式和旧的项目说明文案。

### Task 2: Normalize the main window copy

**Files:**
- Modify: `src/MainWindow.cpp`
- Test: `tests/test_chinese_copy.py`

- [ ] **Step 1: Replace startup, connection, acquisition and settings messages**

Apply these exact replacements in `src/MainWindow.cpp`:

| Existing text | Replacement text |
|---|---|
| `KY-DIMM 就绪（本版本未构建验证；相机行为待硬件确认）` | `KY-DIMM 已就绪（构建和实际相机行为尚待硬件确认）` |
| `相机: 正在连接...` | `相机：正在连接` |
| `连接相机...` | `正在连接相机` |
| `相机: 正在断开...` | `相机：正在断开` |
| `断开相机...` | `正在断开相机` |
| `请先点击“连接相机”，确认相机就绪后再开始采集。` | `请先点击“连接相机”，确认相机已就绪后再开始采集。` |
| `采集状态: 运行中` | `采集状态：运行中` |
| `采集状态: 正在停止...` | `采集状态：正在停止` |
| `停止采集...` | `正在停止采集` |
| `采集运行中，新参数将在下次启动时生效` | `采集正在运行，新参数将在下次启动时生效` |
| `相机: 未连接` | `相机：未连接` |
| `采集状态: 停止` | `采集状态：已停止` |
| `相机已断开` | `相机已断开` |
| `采集已停止` | `采集已停止` |

Keep `tr("参数无效")` and `tr("相机未连接")` as dialog titles; only the body and status copy require the replacements above.

- [ ] **Step 2: Replace preview, status and counter labels**

Apply these exact replacements:

| Existing text | Replacement text |
|---|---|
| `丢帧: %1` | `相机丢帧：%1` |
| `队列丢帧: %1` | `队列丢帧：%1` |
| `相机: %1 / %2` | `相机：%1 / %2` |
| `相机就绪: %1 (Mono8 1920×1200)` | `相机已就绪：%1（Mono8，1920 × 1200）` |
| `相机错误: %1` | `相机错误：%1` |
| `ROI跟踪: 跟踪中` | `ROI 跟踪：跟踪中` |
| `ROI跟踪: 定位中` | `ROI 跟踪：定位中` |
| `ROI跟踪: 未开始` | `ROI 跟踪：未开始` |
| `AOI: %1×%2 @(%3,%4)` | `AOI：%1 × %2，位置 (%3, %4)` |
| `AOI: 全画幅` | `AOI：全画幅` |
| `实际测量率: %1 Hz` | `实际测量率：%1 Hz` |
| `有效帧: %1 / %2` | `有效样本：%1 / %2` |
| `全画幅 1920×1200` | `全画幅 1920 × 1200` |
| `ROI A 64×64` | `ROI A 64 × 64` |
| `ROI B 64×64` | `ROI B 64 × 64` |
| `StarA: 未定位` | `StarA：未定位` |
| `StarB: 未定位` | `StarB：未定位` |
| `StarA: (%1, %2)` | `StarA：(%1, %2)` |
| `StarB: (%1, %2)` | `StarB：(%1, %2)` |
| `最后错误: -` | `最后错误：-` |

Change the button text `断开连接` to `断开相机`, and `停止` to `停止采集`. Do not change their `objectName` values.

- [ ] **Step 3: Replace log and error prefixes without changing control flow**

Use full-width colons in these dynamic messages:

```cpp
appendLog(tr("测量错误：%1").arg(message));
appendLog(tr("相机错误：%1").arg(message));
cameraStateLabel_->setText(tr("相机错误：%1").arg(message));
```

Keep the existing error titles and stop/start sequence unchanged.

- [ ] **Step 4: Run the main-window copy test**

Run:

```powershell
python -m pytest tests/test_chinese_copy.py::test_main_window_uses_consistent_copy -q
```

Expected: PASS.

### Task 3: Normalize the settings dialog copy

**Files:**
- Modify: `src/SettingsDialog.ui`
- Modify: `src/SettingsDialog.cpp`
- Test: `tests/test_chinese_copy.py`

- [ ] **Step 1: Normalize static settings labels and help text**

Replace the corresponding `<string>` contents in `src/SettingsDialog.ui` as follows:

| Existing text | Replacement text |
|---|---|
| `主镜口径 (mm)` | `主镜口径（mm）` |
| `子孔径直径 (mm)` | `子孔径直径（mm）` |
| `两个圆形窗口中心距离 (mm)` | `两个圆形窗口中心距离（mm）` |
| `焦距 (mm)` | `焦距（mm）` |
| `波长 (nm)` | `波长（nm）` |
| `像素尺寸 (µm)` | `像元尺寸（µm）` |
| `天顶角 (°)` | `天顶角（°）` |
| `固定初始值：D=60mm、B=150mm（物理基线）、f=2500mm、λ=550nm、像素 5.86µm。` | `默认值：D = 60 mm、B = 150 mm（物理基线）、f = 2500 mm、λ = 550 nm、像元尺寸 = 5.86 µm。` |
| `传感器宽度 (px)` | `传感器宽度（px）` |
| `传感器高度 (px)` | `传感器高度（px）` |
| `测量帧率 (Hz)` | `测量帧率（Hz）` |
| `全画幅预览帧率 (Hz)` | `全画幅预览帧率（Hz）` |
| `ROI 预览帧率 (Hz)` | `ROI 预览帧率（Hz）` |
| `曝光时间 (ms)` | `曝光时间（ms）` |
| `目标时长 (s)` | `目标时长（s）` |
| `本版本固定为 1920×1200、Mono8。测量帧率是有效性的硬性要求；预览帧率仅影响界面刷新。` | `当前版本固定使用 1920 × 1200、Mono8。测量帧率是有效性的硬性要求；预览帧率仅影响界面刷新。` |
| `ROI 宽度 (px)` | `ROI 宽度（px）` |
| `ROI 高度 (px)` | `ROI 高度（px）` |
| `硬件 AOI 边距 (px)` | `硬件 AOI 边距（px）` |
| `Otsu 直方图分箱` | `Otsu 直方图分箱数` |
| `最小连通域面积 (px)` | `最小连通域面积（px）` |
| `最大连通域面积 (px)` | `最大连通域面积（px）` |
| `小核半径 (px)` | `小核半径（px）` |
| `最小星间距 (px)` | `最小星间距（px）` |
| `边缘触发距离 (px)` | `边缘触发距离（px）` |
| `重定中心冷却 (ms)` | `重定位冷却时间（ms）` |
| `最小移动量 (px)` | `最小移动量（px）` |
| `丢失重定位帧数` | `丢失后重定位帧数` |
| `r0 窗口帧数` | `r0 计算窗口（帧）` |
| `结果更新间隔 (s)` | `结果更新间隔（s）` |
| `tau0 历史 (s)` | `tau0 历史时长（s）` |
| `tau0 最大滞后 (ms)` | `tau0 最大滞后（ms）` |
| `tau0 最少样本` | `tau0 最少样本数` |
| `触发模式` | `触发模式` |
| `帧起始选择器` | `帧起始选择器` |
| `每帧软件触发` | `逐帧软件触发` |
| `软件触发模式下每帧下发一次触发` | `软件触发模式下，每帧发送一次触发信号` |
| `连续采集为相机自由运行（默认）；软件触发由程序按需下发一帧帧起始；硬件触发使用所选线。` | `连续采集时相机自由运行（默认）；软件触发由程序按需发送逐帧触发信号；硬件触发使用所选触发线。` |
| `记录间隔 (s)` | `记录间隔（s）` |
| `浏览...` | `浏览…` |
| `第一版不保存全画幅图像和 ROI 原始图像` | `当前版本不保存全画幅图像和 ROI 原始图像。` |

Leave unchanged the technical values, combo-box values, `objectName` values, and the existing standard-button configuration.

- [ ] **Step 2: Normalize dynamic group, mode and dialog button text**

In `src/SettingsDialog.cpp`, replace only the third `addGroup` title in the existing `rebuildProcessingTab()` body; keep each existing row list and row index unchanged:

```cpp
addGroup(tr("ROI 重定位"),
         {{QStringLiteral("labelProcessingEdgeDistance"),
           QStringLiteral("processingEdgeDistanceSpin")},
          {QStringLiteral("labelProcessingRecenteringConsecutive"),
           QStringLiteral("processingRecenteringConsecutiveSpin")},
          {QStringLiteral("labelProcessingRecenteringCooldown"),
           QStringLiteral("processingRecenteringCooldownMsSpin")},
          {QStringLiteral("labelProcessingMinimumShift"),
           QStringLiteral("processingMinimumShiftSpin")},
          {QStringLiteral("labelProcessingLostFrames"),
           QStringLiteral("processingLostFramesSpin")}},
         2);
```

The other three group titles already match the approved copy and remain `ROI 与 AOI`、`Otsu 与质心`、`大气参数计算`. Keep the existing trigger mode values `连续采集`、`软件触发`、`硬件触发` unchanged. Keep `tr("选择数据输出目录")` unchanged. After the `QDialogButtonBox *buttons` lookup succeeds, set the standard button labels before adding the Apply button:

```cpp
if (QPushButton *okButton = buttons->button(QDialogButtonBox::Ok))
    okButton->setText(tr("确定"));
if (QPushButton *cancelButton = buttons->button(QDialogButtonBox::Cancel))
    cancelButton->setText(tr("取消"));
QPushButton *applyButton =
    buttons->addButton(tr("应用"), QDialogButtonBox::ApplyRole);
```

Do not change the `ApplyRole` connection or `apply()` behavior.

- [ ] **Step 3: Run the settings copy test**

Run:

```powershell
python -m pytest tests/test_chinese_copy.py::test_settings_dialog_uses_consistent_copy -q
```

Expected: PASS.

### Task 4: Normalize runtime validation, calculation, camera and tracking messages

**Files:**
- Modify: `src/AppConfig.cpp`
- Modify: `src/AtmosphereCalculator.cpp`
- Modify: `src/CentroidEngine.cpp`
- Modify: `src/MeasurementWorker.cpp`
- Modify: `src/PylonCamera.cpp`
- Modify: `src/ResultWriter.cpp`
- Modify: `src/TwoStarTracker.cpp`
- Test: `tests/test_chinese_copy.py`

- [ ] **Step 1: Update configuration validation messages**

In `src/AppConfig.cpp`, use these exact user-facing messages:

```cpp
errors << QStringLiteral("主镜口径必须大于 0 mm");
errors << QStringLiteral("子孔径直径必须大于 0 mm");
errors << QStringLiteral("两个圆形窗口中心距离必须大于 0 mm");
errors << QStringLiteral("望远镜焦距必须大于 0 mm");
errors << QStringLiteral("波长必须大于 0 nm");
errors << QStringLiteral("像元尺寸必须大于 0 µm");
errors << QStringLiteral("当前版本仅允许 1920 × 1200 全画幅");
errors << QStringLiteral("第一版仅允许 Mono8");
errors << QStringLiteral("测量采样率必须至少为 100 Hz");
errors << QStringLiteral("预览刷新率必须大于 0 Hz");
errors << QStringLiteral("单次曝光时间必须在 (0, 10] ms 范围内");
errors << QStringLiteral("目标样本数和目标时长必须大于 0");
errors << QStringLiteral("软件 ROI 至少为 16 × 16 像素");
errors << QStringLiteral("Otsu 直方图分箱数必须在 256 至 16384 之间");
errors << QStringLiteral("连通域面积范围无效");
errors << QStringLiteral("小核半径必须在 1 至 20 像素之间");
errors << QStringLiteral("r0 计算窗口至少需要 2 个有效样本");
errors << QStringLiteral("数据输出目录不能为空");
errors << QStringLiteral("当前版本不允许保存原始图像");
```

- [ ] **Step 2: Update calculation and tracking state messages**

In `src/AtmosphereCalculator.cpp`, replace the status messages with:

```cpp
QStringLiteral("实际测量率为 %1 Hz，低于要求的 %2 Hz")
QStringLiteral("等待足够的有效样本（%1 / %2）")
QStringLiteral("差分方差为零，无法计算 r0")
QStringLiteral("有效")
```

In `src/CentroidEngine.cpp`, change only the diagnostic `无历史，选择最强候选` to `无历史跟踪记录，选择最强候选`; retain `ROI 为空`、`未找到连通域` and `小核加权强度不足`.

In `src/MeasurementWorker.cpp`, change `硬件 AOI 配置失败: %1` to `硬件 AOI 配置失败：%1`; retain `测量线程收到无效帧`.

In `src/TwoStarTracker.cpp`, use:

```cpp
QStringLiteral("全画幅未找到候选目标")
QStringLiteral("经过亮度/形状一致性过滤后，候选目标不足")
QStringLiteral("未找到满足最小间距要求的双星对")
QStringLiteral("全画幅定位成功")
QStringLiteral("软件 ROI 超出当前 AOI")
QStringLiteral("有效双星")
QStringLiteral("StarB 丢失")
QStringLiteral("StarA 丢失")
```

- [ ] **Step 3: Update camera and result-writer messages**

In `src/PylonCamera.cpp`, retain all SDK node names and use these text forms:

```cpp
QStringLiteral("未找到任何相机设备")
QStringLiteral("未找到 Basler acA1920-40gm 相机")
QStringLiteral("相机未打开")
QStringLiteral("相机不支持 PixelFormat 节点")
QStringLiteral("单次曝光时间必须在 (0, 10] ms 范围内")
QStringLiteral("相机不支持 ExposureTime 节点")
QStringLiteral("采样帧率必须大于 0 Hz")
QStringLiteral("相机采集帧率范围为 %1～%2 Hz，无法完全达到 %3 Hz")
QStringLiteral("相机不支持 AcquisitionFrameRate 节点")
```

All existing repeated `相机未打开` messages remain exactly that text. In `src/ResultWriter.cpp`, use:

```cpp
QStringLiteral("输出目录为空，无法创建结果目录")
QStringLiteral("无法创建结果目录：%1")
QStringLiteral("无法打开输出文件：%1")
QStringLiteral("无法写入 run_metadata.json")
QStringLiteral("run_metadata.json 写入失败")
```

Do not change error return behavior, file names, or JSON/CSV keys.

- [ ] **Step 4: Run the runtime copy test**

Run:

```powershell
python -m pytest tests/test_chinese_copy.py::test_runtime_copy_uses_consistent_terms -q
```

Expected: PASS.

### Task 5: Clean the historical UI and project documentation

**Files:**
- Modify: `src/UI_New.ui`
- Modify: `KY-DIMM-项目介绍.md`
- Test: `tests/test_chinese_copy.py`

- [ ] **Step 1: Rename the historical window title only**

In `src/UI_New.ui`, replace:

```xml
<string>UI_New</string>
```

with:

```xml
<string>KY-DIMM</string>
```

Do not add widgets, menus, signals, or layout content. This file is historical and is not part of the `KY_DIMM` executable source list.

- [ ] **Step 2: Normalize the project document as UTF-8**

Edit `KY-DIMM-项目介绍.md` with `apply_patch` while preserving its existing facts and section structure. Apply these content rules throughout the document:

- Keep the title `# KY-DIMM 项目介绍` and `更新时间：2026-08-12`.
- Replace `子孔径直径`/`子瞳直径` references with the single hardware term `子孔径直径`.
- Replace `窗口中心距离` with `两个圆形窗口中心距离` where it refers to the physical optical baseline.
- Replace `重定中心` with `重定位` and `ROI跟踪` with `ROI 跟踪`.
- Use `1920 × 1200`, `5.86 µm`, `254 mm`, `60 mm`, `150 mm`, `2500 mm`, and `550 nm` with spaces between values and units.
- Use `有效样本` for the rolling measurement window, while retaining `有效帧` only for raw frame counts.
- Fix Markdown links so their visible labels are the actual file names, for example change a link label from `[MainWindow.*]` to `[MainWindow.cpp]` when the target is `src/MainWindow.cpp`, without changing link targets.
- Preserve the stated hardware-validation limitations and the existing test result as historical information; do not claim hardware validation or a new test count.
- Save the file as strict UTF-8 without a BOM.

- [ ] **Step 3: Run the historical UI and document test**

Run:

```powershell
python -m pytest tests/test_chinese_copy.py::test_legacy_window_and_project_document_are_clean_utf8 -q
```

Expected: PASS.

### Task 6: Run the complete verification suite

**Files:**
- Test: `tests/test_chinese_copy.py`
- Verify: all modified files in `src`, `KY-DIMM-项目介绍.md`, and `src/UI_New.ui`

- [ ] **Step 1: Validate UTF-8 and XML without changing files**

Run:

```powershell
@(
  'src/MainWindow.cpp',
  'src/SettingsDialog.cpp',
  'src/AppConfig.cpp',
  'src/AtmosphereCalculator.cpp',
  'src/CentroidEngine.cpp',
  'src/MeasurementWorker.cpp',
  'src/PylonCamera.cpp',
  'src/ResultWriter.cpp',
  'src/TwoStarTracker.cpp',
  'src/SettingsDialog.ui',
  'src/UI_New.ui',
  'KY-DIMM-项目介绍.md'
) | ForEach-Object {
    $bytes = [System.IO.File]::ReadAllBytes($_)
    [System.Text.UTF8Encoding]::new($false, $true).GetString($bytes) | Out-Null
    "UTF8 OK: $_"
  }
[xml](Get-Content -Raw -Encoding utf8 src/SettingsDialog.ui) | Out-Null
[xml](Get-Content -Raw -Encoding utf8 src/UI_New.ui) | Out-Null
```

Expected: one `UTF8 OK` line per file and no XML parsing error.

- [ ] **Step 2: Scan for the explicitly retired copy**

Run:

```powershell
rg -n -S 'ROI跟踪|重定中心|浏览\.\.\.|相机: |采集状态: |实际测量率: |有效帧: |1920 x 1200|16 x 16|像元尺寸必须大于 0 um' src KY-DIMM-项目介绍.md
```

Expected: no matches. A match in a historical plan is allowed because `docs/superpowers/plans` is intentionally outside the scope.

- [ ] **Step 3: Run all Python tests**

Run:

```powershell
python -m pytest -q
```

Expected: all existing tests and the new copy tests pass.

- [ ] **Step 4: Build the existing configured CMake tree**

Run:

```powershell
cmake --build build --config Release --parallel
```

Expected: `KY_DIMM.vcxproj` builds successfully and the existing deployment post-build step completes. If the command fails because Qt, OpenCV, pylon, or the Visual Studio generator is unavailable, record the exact dependency error and do not alter `CMakeLists.txt` to bypass it.

- [ ] **Step 5: Review the scoped diff**

Run:

```powershell
git -c safe.directory='E:/Softwoare/visual studio/project' diff -- UI/UI_pylon/src UI/UI_pylon/tests UI/UI_pylon/KY-DIMM-项目介绍.md
git -c safe.directory='E:/Softwoare/visual studio/project' status --short -- UI/UI_pylon
```

Expected: the diff contains only the approved copy/documentation files and no changes to unrelated projects. Because the repository index is owned by the parent workspace and may be read-only in this environment, do not use `git reset --hard`, `git checkout --`, or any command that discards unrelated changes.

- [ ] **Step 6: Commit only if the repository grants index write access**

If Git permits staging from the repository root, run:

```powershell
git -c safe.directory='E:/Softwoare/visual studio/project' add -- UI/UI_pylon/src/MainWindow.cpp UI/UI_pylon/src/SettingsDialog.ui UI/UI_pylon/src/SettingsDialog.cpp UI/UI_pylon/src/AppConfig.cpp UI/UI_pylon/src/AtmosphereCalculator.cpp UI/UI_pylon/src/CentroidEngine.cpp UI/UI_pylon/src/MeasurementWorker.cpp UI/UI_pylon/src/PylonCamera.cpp UI/UI_pylon/src/ResultWriter.cpp UI/UI_pylon/src/TwoStarTracker.cpp UI/UI_pylon/src/UI_New.ui UI/UI_pylon/tests/test_chinese_copy.py UI/UI_pylon/KY-DIMM-项目介绍.md UI/UI_pylon/docs/superpowers/plans/2026-08-13-chinese-interface-copy-cleanup-plan.md
git -c safe.directory='E:/Softwoare/visual studio/project' commit -m "fix: unify Chinese interface copy"
```

Expected: one commit containing only the scoped files. If staging fails with `index.lock` permission denied, leave the files in the workspace and report that commit creation was blocked by repository permissions; do not touch the parent repository's unrelated staged or untracked changes.

## Plan self-review

- Spec coverage: main window, settings dialog, runtime strings, historical UI, UTF-8 document cleanup, static tests, XML validation, full pytest suite, CMake build, and scoped diff review each have an explicit task.
- Completeness scan: no implementation step relies on an unspecified behavior; every replacement has an exact target text or an exact command.
- Consistency: the plan uses `ROI 重定位`, `ROI 跟踪`, `有效样本`, `子孔径直径`, `1920 × 1200`, and `µm` consistently across tests, source replacements, and documentation rules.
- Scope: no task changes algorithm control flow, configuration fields, serialization keys, object names, or unrelated parent-repository files.
