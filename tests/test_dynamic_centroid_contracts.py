from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"


def read_source(name: str) -> str:
    return (SRC / name).read_text(encoding="utf-8")


def test_configuration_has_one_preview_rate_and_dynamic_centroid_window():
    types = read_source("ProcessingTypes.h")
    app_config = read_source("AppConfig.cpp")
    settings = read_source("SettingsDialog.cpp")
    settings_ui = read_source("SettingsDialog.ui")

    assert "double previewRateHz = 20.0" in types
    assert "int centroidKernelRadiusPx = 3" in types
    assert "previewRateHz" in app_config
    assert "centroidKernelRadiusPx" in app_config
    assert 'name="acquisitionPreviewRateSpin"' in settings_ui
    assert 'name="aoiCentroidKernelRadiusSpin"' in settings_ui
    assert "acquisitionPreviewRateSpin" in settings
    assert "aoiCentroidKernelRadiusSpin" in settings

    for legacy in (
        "fullFramePreviewRateHz",
        "roiPreviewRateHz",
        "roiWidthPx",
        "roiHeightPx",
        "roiRecenteringDistanceToEdgePx",
        "roiRecenteringConsecutiveFrames",
        "roiRecenteringCooldownMs",
        "roiRecenteringMinimumShiftPx",
    ):
        assert legacy not in types


def test_aoi_tracking_has_no_software_roi_recenter_state():
    tracker = read_source("TwoStarTracker.cpp")
    header = read_source("TwoStarTracker.h")

    assert "extractCandidates" in tracker
    assert "processAoiFrame" in tracker
    assert "frame.mono8" in tracker
    assert "centroidKernelRadiusPx" in tracker
    assert "needsHardwareAoiUpdate" in tracker
    assert "updateRecenteringState" not in tracker
    assert "roiA_" not in tracker
    assert "roiB_" not in tracker
    assert "roiA_" not in header
    assert "roiB_" not in header
    assert "roiA()" not in header
    assert "roiB()" not in header


def test_centroid_refinement_uses_a_configurable_ui2_compatible_kernel():
    header = read_source("CentroidEngine.h")
    source = read_source("CentroidEngine.cpp")

    assert "refineInKernel" in header
    assert "refineInKernel" in source
    assert "centroidKernelRadiusPx" in source
    assert "row = cy - radius" in source
    assert "column = cx - radius" in source


def test_display_contains_only_hardware_aoi_and_red_centroid_crosses():
    types = read_source("ProcessingTypes.h")
    adapter = read_source("ImageDisplayAdapter.cpp")
    mailbox = read_source("DisplayMailbox.h")
    main_window = read_source("MainWindow.cpp")

    assert "struct DisplayOverlay" in types
    assert "hasCentroids" in types
    assert "QColor(255, 70, 70" in adapter
    assert "drawLine" in adapter
    assert "overlay.roiA" not in adapter
    assert "overlay.roiB" not in adapter
    assert "cv::Mat roiAMono8" not in mailbox
    assert "cv::Mat roiBMono8" not in mailbox
    assert "refreshRoiPreview" not in main_window
    assert "roiTimer_" not in main_window
    assert "目标 ROI 与质心状态" not in main_window


def test_result_csv_and_metadata_do_not_publish_software_roi_coordinates():
    writer = read_source("ResultWriter.cpp")

    assert "centroidKernelRadiusPx" in writer
    assert "previewRateHz" in writer
    assert "roi_a_x" not in writer
    assert "roi_a_y" not in writer
    assert "roi_b_x" not in writer
    assert "roi_b_y" not in writer


def test_settings_ui_removes_software_roi_controls_and_uses_aoi_controls():
    settings_ui = read_source("SettingsDialog.ui")
    settings_cpp = read_source("SettingsDialog.cpp")

    assert "全画幅质心预览帧率" in settings_ui
    assert "质心计算核半径" in settings_ui
    assert "半径 3 = 7 × 7" in settings_ui
    assert "硬件 AOI 更新安全边界距离" in settings_ui
    assert "只在质心核接近 AOI 边界时更新硬件 AOI" in settings_ui

    for legacy in (
        "acquisitionFullFramePreviewRateSpin",
        "acquisitionRoiPreviewRateSpin",
        "aoiRoiWidthSpin",
        "aoiRoiHeightSpin",
        "aoiRecenteringDistanceSpin",
        "aoiRecenteringConsecutiveSpin",
        "aoiRecenteringCooldownSpin",
        "aoiRecenteringShiftSpin",
        "processingRoiWidthSpin",
        "processingRoiHeightSpin",
        "processingAoiMarginSpin",
        "processingEdgeDistanceSpin",
        "processingRecenteringConsecutiveSpin",
        "processingRecenteringCooldownMsSpin",
        "processingMinimumShiftSpin",
        "processingLostFramesSpin",
    ):
        assert legacy not in settings_ui
        assert legacy not in settings_cpp


def test_atmosphere_projection_uses_the_configured_baseline_angle():
    types = read_source("ProcessingTypes.h")
    calculator = read_source("AtmosphereCalculator.cpp")
    app_config = read_source("AppConfig.cpp")
    settings = read_source("SettingsDialog.cpp")
    settings_ui = read_source("SettingsDialog.ui")

    assert "double baselineAngleDeg = 0.0" in types
    assert "optical.baselineAngleDeg" in calculator
    assert 'QStringLiteral("baselineAngleDeg")' in app_config
    assert "physicalBaselineAngleDegSpin" in settings
    assert 'name="physicalBaselineAngleDegSpin"' in settings_ui
