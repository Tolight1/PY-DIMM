from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"


def read_source(name: str) -> str:
    return (SRC / name).read_text(encoding="utf-8")


def test_run_outputs_structured_aoi_events_and_frame_generation_context():
    types = read_source("ProcessingTypes.h")
    writer_header = read_source("ResultWriter.h")
    writer = read_source("ResultWriter.cpp")
    worker_header = read_source("MeasurementWorker.h")
    main = read_source("main.cpp")

    assert "struct AoiEvent" in types
    assert "configurationGeneration" in types
    assert "QRect sourceRect" in types
    assert "Q_DECLARE_METATYPE(AoiEvent)" in types
    assert "QFile aoiEventsFile_" in writer_header
    assert "appendAoiEvent" in writer_header
    assert "aoi_events.csv" in writer
    assert "configuration_generation" in writer
    assert "source_rect_x" in writer
    assert "source_rect_y" in writer
    assert "source_rect_width" in writer
    assert "source_rect_height" in writer
    assert "void aoiEvent(AoiEvent event);" in worker_header
    assert 'qRegisterMetaType<AoiEvent>("AoiEvent")' in main


def test_aoi_event_log_covers_request_apply_failure_relocalize_and_recovery():
    worker = read_source("MeasurementWorker.cpp")
    main_window = read_source("MainWindow.cpp")

    for event_name in (
        "request",
        "applied",
        "failed",
        "relocalize",
        "first_valid_frame",
        "sample_rejected",
    ):
        assert f'QStringLiteral("{event_name}")' in worker
    assert "staleFramesDropped" in worker
    assert "aoiEvent" in main_window
    assert "appendAoiEvent" in main_window


def test_aoi_log_has_unique_epoch_transition_and_actual_active_aoi_context():
    types = read_source("ProcessingTypes.h")
    writer = read_source("ResultWriter.cpp")
    worker = read_source("MeasurementWorker.cpp")

    for field in ("aoiEpoch", "transitionId", "differentialJumpPx"):
        assert field in types
        assert field in writer
    assert "activeAppliedAoi_" in worker
    assert "event.appliedAoi = activeAppliedAoi_" in worker
    assert "aoi_epoch" in writer
    assert "aoi_transition_id" in writer
    assert "differential_jump_px" in writer


def test_differential_jump_is_rejected_without_resetting_the_measurement_window():
    processing_types = read_source("ProcessingTypes.h")
    app_config = read_source("AppConfig.cpp")
    tracker_header = read_source("TwoStarTracker.h")
    tracker = read_source("TwoStarTracker.cpp")
    worker = read_source("MeasurementWorker.cpp")
    settings = read_source("SettingsDialog.cpp")
    settings_ui = read_source("SettingsDialog.ui")

    assert "maximumDifferentialJumpPx" in processing_types
    assert "maximumDifferentialJumpPx" in app_config
    assert "differentialContinuityRejected" in tracker_header
    assert "differentialContinuityRejected" in tracker
    assert "maximumDifferentialJumpPx" in tracker
    assert 'QStringLiteral("sample_rejected")' in worker
    assert "calculator_.append(sample)" in worker
    assert "calculator_.reset();" in worker
    assert 'name="aoiMaximumDifferentialJumpSpin"' in settings_ui
    assert "aoiMaximumDifferentialJumpSpin" in settings


def test_aoi_transition_burst_is_recorded_frame_by_frame():
    types = read_source("ProcessingTypes.h")
    writer_header = read_source("ResultWriter.h")
    writer = read_source("ResultWriter.cpp")
    worker_header = read_source("MeasurementWorker.h")
    worker = read_source("MeasurementWorker.cpp")
    main = read_source("main.cpp")
    main_window = read_source("MainWindow.cpp")

    assert "struct AoiTransitionSample" in types
    for field in (
        "frameIndexAfterApply",
        "differentialJumpPx",
        "sampleAccepted",
        "sourceRect",
    ):
        assert field in types
    assert "aoiTransitionSamplesFile_" in writer_header
    assert "appendAoiTransitionSample" in writer_header
    assert "aoi_transition_samples.csv" in writer
    assert "emit aoiTransitionSample" in worker
    assert "aoiTransitionDiagnosticFramesRemaining_" in worker_header
    assert "kAoiTransitionDiagnosticFrameCount = 50" in types
    assert "aoiTransitionSample" in main_window
    assert 'qRegisterMetaType<AoiTransitionSample>("AoiTransitionSample")' in main


def test_maximum_differential_jump_default_is_ten_pixels():
    processing_types = read_source("ProcessingTypes.h")
    settings_ui = read_source("SettingsDialog.ui")

    assert "maximumDifferentialJumpPx = 10.0" in processing_types
    assert '<property name="value"><double>10.0</double></property>' in settings_ui


def test_differential_baseline_gate_has_configurable_consecutive_violations():
    processing_types = read_source("ProcessingTypes.h")
    app_config = read_source("AppConfig.cpp")
    tracker_header = read_source("TwoStarTracker.h")
    tracker = read_source("TwoStarTracker.cpp")
    settings = read_source("SettingsDialog.cpp")
    settings_ui = read_source("SettingsDialog.ui")
    writer = read_source("ResultWriter.cpp")

    assert "differentialBaselineViolationFrames = 5" in processing_types
    assert "differentialBaselineViolationFrames" in app_config
    assert "differentialBaselineHistory_" in tracker_header
    assert "kDifferentialBaselineWindowFrames = 20" in processing_types
    assert "componentWiseMedian" in tracker
    assert "differentialBaselineDeviationPx" in tracker
    assert "differentialBaselineViolationFrames" in tracker
    assert 'name="aoiDifferentialBaselineViolationFramesSpin"' in settings_ui
    assert "aoiDifferentialBaselineViolationFramesSpin" in settings
    assert "differential_baseline_deviation_px" in writer
    assert "differential_baseline_violation_frames" in writer


def test_physical_defaults_match_miyun_polaris_observation_setup():
    processing_types = read_source("ProcessingTypes.h")
    settings_ui = read_source("SettingsDialog.ui")

    assert "subApertureDiameterMm = 80.0" in processing_types
    assert "baselineSeparationMm = 170.0" in processing_types
    assert "zenithAngleDeg = 49.6" in processing_types
    assert "北京市密云区北极星" in settings_ui
    assert "80 mm" in settings_ui
    assert "170 mm" in settings_ui
    assert "49.6°" in settings_ui
