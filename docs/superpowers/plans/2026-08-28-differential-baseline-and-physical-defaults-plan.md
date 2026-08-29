# Differential Baseline and Physical Defaults Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Prevent gradual double-star differential drift from silently entering the rolling measurement path while updating the requested DIMM physical defaults.

**Architecture:** Keep the existing single-frame differential-jump gate for abrupt association errors. Add a bounded 20-sample component-wise median baseline in `TwoStarTracker`; a configurable consecutive-violation count controls when persistent baseline deviation is rejected and contributes to existing relocalization handling. Persist and expose only the consecutive count in the AOI settings page, and publish baseline deviation/count in structured AOI diagnostics. Physical defaults remain editable and use 80 mm, 170 mm, and 49.6° for the Miyun Polaris observation setup.

**Tech Stack:** Qt 6/C++17, QSettings, Qt Designer `.ui`, Python pytest source-contract tests, CMake/VS18 x64 Release.

---

### Task 1: Add red contract tests

**Files:**
- Modify: `tests/test_aoi_logging_contracts.py`
- Modify: `tests/test_chinese_copy.py`
- Modify: `tests/test_physics_calculator.py`

- [ ] **Step 1: Assert the new processing setting, median baseline, diagnostics, and UI binding.**

  The contract test must require `differentialBaselineViolationFrames` in `ProcessingConfig`, QSettings save/load, validation, tracker state, AOI settings read/populate paths, metadata, and the transition CSV fields. It must also require a bounded median helper/window and the AOI widget named `aoiDifferentialBaselineViolationFramesSpin`.

- [ ] **Step 2: Update physical-default expectations.**

  Change the contract expectations from 60/150/0 to 80/170/49.6 and require the physical help text to identify the 49.6° value as the Miyun Polaris default while retaining editable zenith angle.

- [ ] **Step 3: Run the focused tests and verify the expected RED failure.**

  Run `python -m pytest -q tests/test_aoi_logging_contracts.py tests/test_chinese_copy.py tests/test_physics_calculator.py`.
  Expected result: failure because the new setting/baseline/default source contracts are not implemented yet.

### Task 2: Implement configuration and UI wiring

**Files:**
- Modify: `src/ProcessingTypes.h`
- Modify: `src/AppConfig.cpp`
- Modify: `src/SettingsDialog.cpp`
- Modify: `src/SettingsDialog.ui`
- Modify: `src/ResultWriter.cpp`

- [ ] **Step 1: Add `differentialBaselineViolationFrames = 5` and validate it as a positive frame count.**

  Save/load it under the existing `processing` QSettings group and include it in run metadata. Do not change existing saved user values.

- [ ] **Step 2: Add an AOI settings spin box with range 1–10000 and default 5.**

  Read and populate it through `SettingsDialog.cpp`. Update the differential-jump tooltip/help text to distinguish abrupt one-frame jumps from persistent median-baseline deviations.

- [ ] **Step 3: Change editable physical defaults to 80.0 mm, 170.0 mm, and 49.6°.**

  Update the C++ defaults and `.ui` initial values/help text. Keep the physical fields editable and preserve existing QSettings values when present.

### Task 3: Implement the bounded median baseline gate

**Files:**
- Modify: `src/TwoStarTracker.h`
- Modify: `src/TwoStarTracker.cpp`
- Modify: `src/ProcessingTypes.h`

- [ ] **Step 1: Add bounded differential history and diagnostic state.**

  Keep the last 20 accepted full-frame differential vectors, compute component-wise medians, and expose the current baseline deviation and consecutive count through `TrackerUpdate`, `AoiEvent`, and `AoiTransitionSample`.

- [ ] **Step 2: Preserve the existing abrupt-jump behavior.**

  A jump above `maximumDifferentialJumpPx` remains an immediate sample rejection. A baseline deviation uses the same configured pixel threshold but only rejects after `differentialBaselineViolationFrames` consecutive deviations. Common translation of both stars therefore does not affect the baseline.

- [ ] **Step 3: Keep AOI and measurement lifecycle independent.**

  Do not clear the rolling atmosphere calculator window on AOI application or full-frame relocalization. Re-seed only the tracker’s differential baseline when a full-frame pair is newly confirmed; retain the existing relocalization and sample-window behavior.

### Task 4: Verify and review

**Files:**
- No additional source files.

- [ ] **Step 1: Run the focused tests and then `python -m pytest -q`.**
- [ ] **Step 2: Run the exact `uic` command for `src/SettingsDialog.ui`.**
- [ ] **Step 3: Re-read `CODEX_BUILD_HANDOFF.md`, run the VS18 x64 Release build, and inspect executable/deployment artifacts.**
- [ ] **Step 4: Run the no-camera startup smoke test; report it separately from real-camera validation.**
- [ ] **Step 5: Review `git diff` and `git status`; preserve all pre-existing modifications and do not commit, push, pull, merge, or clean.**
