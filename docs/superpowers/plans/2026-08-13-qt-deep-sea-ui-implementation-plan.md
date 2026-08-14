# Deep-Sea Instrument UI Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task with verification checkpoints.

**Goal:** 将 KY-DIMM 的 Qt Widgets 主界面与设置对话框统一为深海科技仪器风格，强化实时观测、结果读数和诊断信息的层级，同时不改变算法、线程或数据行为。

**Architecture:** 保留现有动态构建的 MainWindow 与 Qt Designer 生成的 SettingsDialog 结构，只增加稳定的 Qt dynamic properties 和布局层级。所有颜色、状态、输入框、卡片、日志和分割器样式集中在 UiTheme，业务代码只负责声明面板角色和布局语义。

**Tech Stack:** C++17, Qt 6 Widgets, Qt Style Sheets, existing direct-import Python source-contract tests, MSVC Release build.

---

### Task 1: Add the visual contract tests

**Files:**
- Create: `tests/test_ui_visual_contract.py`

- [ ] **Step 1: Write the failing test**

  Assert that the source contains the approved deep-sea tokens, focus/disabled states, canvas/result/diagnostic selectors, explicit main splitter proportions, diagnostic log section, settings cards, and a resizable settings dialog.

- [ ] **Step 2: Run the test to verify it fails**

  Run the repository's direct-import test runner and confirm the new contract fails because the new selectors/properties are not present yet.

### Task 2: Implement the shared visual system

**Files:**
- Modify: `src/UiTheme.cpp`

- [ ] **Step 1: Replace the legacy palette tokens with the approved deep-sea palette.**
- [ ] **Step 2: Add QSS for panel roles, status badges, result values, focus states, inputs, tabs, progress bars, logs, scrollbars, and splitter handles.**
- [ ] **Step 3: Apply matching Fusion palette colors so native controls and QSS controls share the same visual language.**
- [ ] **Step 4: Run the visual contract tests.**

### Task 3: Restructure the main observation console

**Files:**
- Modify: `src/MainWindow.cpp`

- [ ] **Step 1: Add title/subtitle and status-badge hierarchy to the top bar.**
- [ ] **Step 2: Mark the full-frame, results, ROI, and diagnostic surfaces with panel roles; add compact section headers and the 58/42 observation split.**
- [ ] **Step 3: Replace the crowded bottom row with a diagnostic panel containing the log and a readable status stack, preserving all existing widget pointers and signal-driven behavior.**
- [ ] **Step 4: Run source-contract tests and compile the affected translation unit.**

### Task 4: Style the settings dialog as grouped instrument configuration

**Files:**
- Modify: `src/SettingsDialog.cpp`
- Modify: `src/SettingsDialog.ui`

- [ ] **Step 1: Give the settings dialog a usable 1280x800-era minimum/initial size and consistent tab/card spacing.**
- [ ] **Step 2: Mark dynamically rebuilt processing groups as settings cards with readable internal margins and spacing.**
- [ ] **Step 3: Keep all existing object names, parameter ranges, and read/write behavior unchanged.**
- [ ] **Step 4: Run the visual contract tests and `uic` against `SettingsDialog.ui`.**

### Task 5: Verify the complete Release artifact

**Files:**
- No additional source files.

- [ ] **Step 1: Run all direct-import Python tests.**
- [ ] **Step 2: Run `uic` and the existing handoff-compatible MSVC/Qt Release build flow.**
- [ ] **Step 3: Verify the produced `build/Release/KY_DIMM.exe` and required runtime DLLs.**
- [ ] **Step 4: Report exact verification results and the changed files.**

## Self-review

- The approved palette, 8/12/16/24 spacing direction, 6–8 px card radius, real-time observation emphasis, diagnostics emphasis, 58/42 main split, settings card grouping, and 1280x800 target are covered by Tasks 2–4.
- Algorithm, centroid, acquisition, worker, persistence, and result-writing code are outside the change set.
- The plan uses no new asset, font, dependency, or animation requirement.
- The only test fixture is a source-level contract, matching the repository's existing test strategy.
