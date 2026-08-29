from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"


def read_source(name: str) -> str:
    return (SRC / name).read_text(encoding="utf-8")


def test_full_frame_uses_two_largest_connected_components_without_ratio_filters():
    source = read_source("TwoStarTracker.cpp")

    assert "std::sort" in source
    assert "integratedIntensity" in source
    assert "已选择面积最大的两个有效连通域" in source
    assert "0.30 * maxIntensity" not in source
    assert "0.30 * maxArea" not in source
    assert "亮度/形状一致性过滤" not in source


def test_full_frame_candidate_sort_prioritizes_component_area():
    source = read_source("TwoStarTracker.cpp")
    comparator = source.split("std::sort(candidates.begin()", 1)[1].split("return candidates;", 1)[0]

    area_order = comparator.index("lhs.area")
    intensity_order = comparator.index("lhs.integratedIntensity")
    assert area_order < intensity_order


def test_aoi_settings_are_first_class_and_cover_size_and_update_conditions():
    processing_types = read_source("ProcessingTypes.h")
    app_config = read_source("AppConfig.cpp")
    settings_cpp = read_source("SettingsDialog.cpp")
    settings_ui = read_source("SettingsDialog.ui")

    assert "hardwareAoiMaxWidthPx" in processing_types
    assert "hardwareAoiMaxHeightPx" in processing_types
    assert "hardwareAoiMaxWidthPx" in app_config
    assert "hardwareAoiMaxHeightPx" in app_config
    assert 'name="aoiTab"' in settings_ui
    for widget_name in (
        "aoiCentroidKernelRadiusSpin",
        "aoiHardwareMarginSpin",
        "aoiMaxWidthSpin",
        "aoiMaxHeightSpin",
        "aoiLostPairRelocalizationFramesSpin",
        "aoiHardwareUpdateDistanceSpin",
        "aoiHardwareUpdateShiftSpin",
        "aoiHardwareUpdateCooldownSpin",
        "aoiMinimumPeakDistanceSpin",
    ):
        assert f'name="{widget_name}"' in settings_ui
        assert widget_name in settings_cpp


def test_hardware_aoi_update_conditions_are_independent_of_software_roi_updates():
    processing_types = read_source("ProcessingTypes.h")
    tracker_header = read_source("TwoStarTracker.h")
    tracker_source = read_source("TwoStarTracker.cpp")
    settings_cpp = read_source("SettingsDialog.cpp")
    settings_ui = read_source("SettingsDialog.ui")

    for name in (
        "hardwareAoiUpdateDistanceToEdgePx",
        "hardwareAoiUpdateMinimumShiftPx",
        "hardwareAoiUpdateCooldownMs",
    ):
        assert name in processing_types
        assert name in settings_cpp
    assert "needsHardwareAoiUpdate" in tracker_header
    assert "needsHardwareAoiUpdate" in tracker_source
    assert "lastHardwareAoiRequestMs_" in tracker_header
    assert "aoiHardwareUpdateDistanceSpin" in settings_ui
    assert "aoiHardwareUpdateShiftSpin" in settings_ui
    assert "aoiHardwareUpdateCooldownSpin" in settings_ui
    assert "config_.acquisition.enableHardwareAoi &&" in tracker_source
    assert "needsHardwareAoiUpdate" in tracker_source
    assert "centroidKernelRadiusPx" in tracker_source
    assert "updateRecenteringState" not in tracker_source


def test_minimum_peak_distance_is_editable_on_aoi_page():
    app_config = read_source("AppConfig.cpp")
    settings_cpp = read_source("SettingsDialog.cpp")
    settings_ui = read_source("SettingsDialog.ui")

    assert "minimumPeakDistancePx" in app_config
    assert "processing.minimumPeakDistancePx <= 0.0" in app_config
    assert 'name="aoiMinimumPeakDistanceSpin"' in settings_ui
    assert "aoiMinimumPeakDistanceSpin" in settings_cpp


def test_hardware_aoi_reports_camera_readback_to_measurement_and_ui():
    camera_header = read_source("PylonCamera.h")
    camera_worker = read_source("CameraWorker.cpp")
    measurement_worker = read_source("MeasurementWorker.cpp")

    assert "RoiRect *appliedAoi" in camera_header
    assert "activeHardwareAoi" in camera_header
    assert "RoiRect appliedAoi" in camera_worker
    assert "hardwareAoiApplied(appliedAoi" in camera_worker
    assert "activeHardwareAoi" in camera_worker
    assert "starStateChanged" in measurement_worker


def test_aoi_relocalization_does_not_reset_the_rolling_measurement_window():
    source = read_source("MeasurementWorker.cpp")

    assert source.count("calculator_.reset();") == 1
    assert "lastResultTimestampSec_" in source
    assert "timestampSec - lastResultTimestampSec_" in source
    assert "measurementRateHz *" not in source
