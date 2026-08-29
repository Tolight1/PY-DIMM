from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"


def read_source(name: str) -> str:
    return (SRC / name).read_text(encoding="utf-8")


def test_camera_acquisition_rate_is_separate_from_measurement_rate():
    camera_types = read_source("CameraTypes.h")
    pylon = read_source("PylonCamera.cpp")
    main_header = read_source("MainWindow.h")
    main_window = read_source("MainWindow.cpp")

    assert "double acquisitionRateHz = 0.0;" in camera_types
    assert "double targetRateHz = 0.0;" in camera_types
    assert "double resultingFrameRateHz = 0.0;" in camera_types
    assert "acquisitionRateHz" in pylon
    assert "ResultingFrameRateAbs" in pylon
    assert "cameraRateLabel_" in main_header
    assert "相机采集率：" in main_window
    assert "预计 %3 Hz" in main_window
    assert "有效测量率：" in main_window
    assert "实际测量率：" not in main_window


def test_atmosphere_result_uses_sample_window_without_rate_hard_gate():
    source = read_source("AtmosphereCalculator.cpp")

    assert "if (measuredRateHz < config_.acquisition.measurementRateHz)" not in source
    assert "n < static_cast<std::size_t>(config_.processing.r0WindowFrames)" in source


def test_camera_diagnostics_names_the_camera_rate_column():
    source = read_source("ResultWriter.cpp")

    assert 'QStringLiteral("acquisition_rate_hz")' in source
    assert 'QStringLiteral("target_rate_hz")' in source
    assert 'QStringLiteral("resulting_frame_rate_hz")' in source
    assert "stats.acquisitionRateHz" in source
