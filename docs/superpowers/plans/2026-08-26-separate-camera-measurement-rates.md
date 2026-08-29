# Separate Camera and Measurement Rates Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Display camera acquisition rate separately from valid measurement rate and publish DIMM parameters once the effective sample window is full, regardless of the configured rate gate.

**Architecture:** Measure successful camera callback timestamps in `PylonCamera::Impl` and expose the value through `CameraStatistics::acquisitionRateHz`. Keep `MeasurementWorker::currentRateHz()` as the valid-pair rate, display both values in `MainWindow`, and remove only the rate-based invalidation from `AtmosphereCalculator::calculate()` while retaining the sample-count and tau0-specific guards.

**Tech Stack:** C++17, Qt, Basler pylon, OpenCV, CMake/MSBuild, Python pytest source-contract tests.

---

### Task 1: Add failing source-contract tests

**Files:**
- Modify: `tests/test_compile_contracts.py`
- Modify: `tests/test_chinese_copy.py`

- [ ] **Step 1: Add the expected camera-rate data path test**

Add a test that reads `CameraTypes.h`, `PylonCamera.cpp`, `MainWindow.h`, and `MainWindow.cpp`, then asserts the new `acquisitionRateHz` field, its camera-side update, the GUI slot, and a dedicated `cameraRateLabel_`.

```python
def test_camera_acquisition_rate_is_separate_from_measurement_rate():
    camera_types = read_source("CameraTypes.h")
    pylon = read_source("PylonCamera.cpp")
    main_header = read_source("MainWindow.h")
    main_window = read_source("MainWindow.cpp")

    assert "double acquisitionRateHz = 0.0;" in camera_types
    assert "acquisitionRateHz" in pylon
    assert "cameraRateLabel_" in main_header
    assert "相机采集率：" in main_window
    assert "有效测量率：" in main_window
```

- [ ] **Step 2: Add the expected calculation-gate test**

Add a test that inspects `AtmosphereCalculator.cpp` and asserts that the old hard rate comparison is absent while the sample-count guard remains.

```python
def test_atmosphere_result_uses_sample_window_without_rate_hard_gate():
    source = read_source("AtmosphereCalculator.cpp")

    assert "if (measuredRateHz < config_.acquisition.measurementRateHz)" not in source
    assert "n < static_cast<std::size_t>(config_.processing.r0WindowFrames)" in source
```

- [ ] **Step 3: Extend the UI wording assertions**

Add `相机采集率：` and `有效测量率：` to `test_main_window_uses_consistent_copy()` and keep `实际测量率：` out of the main-window source.

- [ ] **Step 4: Run the focused tests and verify RED**

Run `python -m pytest -q tests/test_compile_contracts.py tests/test_chinese_copy.py`.

Expected result before production changes: failures for the missing camera-rate field/label and the still-present rate gate, without collection errors.

### Task 2: Implement camera-side acquisition-rate statistics

**Files:**
- Modify: `src/CameraTypes.h`
- Modify: `src/PylonCamera.cpp`
- Modify: `src/ResultWriter.cpp`

- [ ] **Step 1: Add the explicit statistics field**

Add `double acquisitionRateHz = 0.0;` to `CameraStatistics`. Keep `AtmosphereResult::measuredRateHz` unchanged because it represents the valid measurement rate in parameter results.

- [ ] **Step 2: Maintain a bounded successful-frame timestamp window**

Add a `std::deque<std::int64_t>` to `PylonCamera::Impl`. In `handleFrame()`, append each successful frame timestamp, trim the deque to 100 entries, and compute `(count - 1) / elapsedSeconds` when at least two timestamps exist and elapsed time is positive. Do not include failed grabs.

- [ ] **Step 3: Update diagnostics output**

Change the acquisition diagnostics CSV header from `measured_rate_hz` to `acquisition_rate_hz` and write `stats.acquisitionRateHz` in the same column position.

- [ ] **Step 4: Run the focused camera contract test**

Run `python -m pytest -q tests/test_compile_contracts.py::test_camera_acquisition_rate_is_separate_from_measurement_rate` and expect PASS after the production change.

### Task 3: Display both rates in the main window

**Files:**
- Modify: `src/MainWindow.h`
- Modify: `src/MainWindow.cpp`

- [ ] **Step 1: Add the camera-rate label**

Declare `QLabel *cameraRateLabel_ = nullptr;`, create a top-bar label initialized to `相机采集率：-- Hz`, and rename the existing label to `有效测量率：-- Hz`.

- [ ] **Step 2: Update camera statistics**

In `onCameraStatsUpdated()`, display `stats.acquisitionRateHz` with one decimal place. Keep existing camera error handling and result-writer calls.

- [ ] **Step 3: Update measurement statistics and stop state**

Display `有效测量率：%1 Hz`, retain the effective-pair progress count, and reset both rate labels to `-- Hz` when acquisition stops or the camera disconnects.

- [ ] **Step 4: Run UI tests**

Run `python -m pytest -q tests/test_compile_contracts.py tests/test_chinese_copy.py` and expect PASS.

### Task 4: Remove only the measurement-rate hard gate

**Files:**
- Modify: `src/AtmosphereCalculator.cpp`
- Test: `tests/test_compile_contracts.py`

- [ ] **Step 1: Delete the rate-based early return**

Keep `result.measuredRateHz = measuredRateHz`, then let the existing `r0WindowFrames` sample-count check run before the existing variance and parameter calculations. Do not change effective-sample append logic, variance checks, or tau0-specific checks.

- [ ] **Step 2: Preserve the sample-count guard**

Ensure `n < r0WindowFrames` still returns `valid=false` with the existing waiting message; once the window is full, calculation proceeds even when `measuredRateHz` is below the configured target.

- [ ] **Step 3: Run the gate regression test**

Run `python -m pytest -q tests/test_compile_contracts.py::test_atmosphere_result_uses_sample_window_without_rate_hard_gate` and expect PASS.

### Task 5: Full verification and handoff

**Files:**
- Inspect: all changed files and current worktree status

- [ ] **Step 1: Read the build handoff**

Read the repository-root `CODEX_BUILD_HANDOFF.md` before any build or validation command and follow its VS18 x64 Release sequence.

- [ ] **Step 2: Run the full Python suite**

Run `python -m pytest -q` and record the actual pass/fail count.

- [ ] **Step 3: Build Release**

Using the handoff's VS18 x64 environment, build `KY_DIMM` into `build\\Release` and record the exit status.

- [ ] **Step 4: Inspect final changes**

Run `git status --short`, `git diff --check`, and `git diff -- src tests docs/superpowers/specs/2026-08-26-separate-camera-measurement-rates-design.md docs/superpowers/plans/2026-08-26-separate-camera-measurement-rates.md`. Confirm all existing user changes remain intact and do not commit or push without authorization.
