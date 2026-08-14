# Centroid Algorithm Alignment Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Align KY-DIMM's full-frame and ROI centroid detection with UI_2's native Otsu thresholding, configurable 8-connectivity, 9–1000 component-area defaults, and 7×7 small-kernel centroid defaults.

**Architecture:** Add one shared `StarSegmentation` implementation for native Otsu plus the sigma/peak threshold floors, and one small `ConnectedDomain` policy header for connectivity and minimum-area defaults. Persist the new processing parameters and expose them in the existing processing settings tab; both detection paths call the same helpers.

**Tech Stack:** C++17, Qt 6, OpenCV 4.12, CMake, Python source-contract tests.

---

### Task 1: Lock the alignment contract with failing tests

**Files:**
- Create: `tests/test_centroid_alignment.py`
- Test: `src/ProcessingTypes.h`, `src/CentroidEngine.cpp`, `src/TwoStarTracker.cpp`, `CMakeLists.txt`

- [ ] Add tests asserting the four defaults, native `THRESH_OTSU`, the sigma/peak floors, shared segmentation usage, sanitized connectivity, and new CMake sources.
- [ ] Run the new test directly and confirm it fails against the current custom-Otsu/fixed-4 implementation.

### Task 2: Add shared segmentation and connectivity policy

**Files:**
- Create: `src/ConnectedDomain.h`
- Create: `src/StarSegmentation.h`
- Create: `src/StarSegmentation.cpp`
- Modify: `src/ProcessingTypes.h`
- Modify: `CMakeLists.txt`

- [ ] Define default connectivity 8 and minimum component area 9, with sanitization accepting only 4 or 8.
- [ ] Implement native OpenCV Otsu followed by `max(otsu, mean + sigma * stddev, mean + peakFraction * (max - mean))`, including threshold clamping when the floor reaches the image maximum.
- [ ] Add the new files to `KY_DIMM_SOURCES`.

### Task 3: Replace both detection paths and persist parameters

**Files:**
- Modify: `src/CentroidEngine.cpp`
- Modify: `src/TwoStarTracker.cpp`
- Modify: `src/AppConfig.cpp`
- Modify: `src/ResultWriter.cpp`

- [ ] Remove the duplicated custom Otsu functions.
- [ ] Use shared segmentation and configurable connectivity in ROI and full-frame detection.
- [ ] Change defaults to connectivity 8, area 9–1000, radius 3; add sigma/peak/connectivity validation, save, load, and metadata fields.

### Task 4: Expose the effective parameters in the settings UI and docs

**Files:**
- Modify: `src/SettingsDialog.ui`
- Modify: `src/SettingsDialog.cpp`
- Modify: `KY-DIMM-项目介绍.md`

- [ ] Replace the misleading histogram-bin control with sigma and peak-fraction controls, add a 4/8 connectivity selector, and wire values into `ProcessingConfig`.
- [ ] Update the processing help text and project description to state the aligned algorithm and defaults.

### Task 5: Verify tests and build the Release package

**Files:**
- Verify: `tests/test_*.py`, `build/Release/KY_DIMM.exe`

- [ ] Run all direct-import Python tests and confirm zero failures.
- [ ] Reconfigure and build with the exact VS18/CMake flow from `E:/Softwoare/visual studio/project/UI/UI_2/CODEX_BUILD_HANDOFF.md` because new source files change CMake.
- [ ] Verify the Release executable timestamp and Qt/OpenCV/pylon/EAF runtime files beside it.

