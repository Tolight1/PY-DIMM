from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"


def read_source(name: str) -> str:
    return (SRC / name).read_text(encoding="utf-8")


def test_processing_defaults_match_ui2_centroid_pipeline():
    source = read_source("ProcessingTypes.h")

    assert "double otsuSigmaThreshold = 4.0" in source
    assert "double otsuPeakFraction = 0.20" in source
    assert "int connectivity = 8" in source
    assert "int otsuMinimumComponentAreaPx = 9" in source
    assert "int otsuMaximumComponentAreaPx = 1000" in source
    assert "int centroidKernelRadiusPx = 3" in source


def test_shared_segmentation_uses_native_otsu_and_reference_threshold_floors():
    source = read_source("StarSegmentation.cpp")

    assert "cv::THRESH_BINARY | cv::THRESH_OTSU" in source
    assert "mean + sigmaThreshold * standardDeviation" in source
    assert "mean + peakFraction * (maximum - mean)" in source
    assert "std::max(otsuThreshold, sigmaFloor)" in source
    assert "std::max(actualThreshold, peakFloor)" in source


def test_both_detection_paths_use_shared_segmentation_and_configured_connectivity():
    centroid = read_source("CentroidEngine.cpp")
    tracker = read_source("TwoStarTracker.cpp")

    for source in (centroid, tracker):
        assert "StarSegmentation::segmentForegroundOtsu" in source
        assert "ConnectedDomain::sanitizeConnectivity(config" in source
        assert "otsuThresholdFromMono8" not in source


def test_new_algorithm_sources_are_compiled():
    cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")

    assert "src/ConnectedDomain.h" in cmake
    assert "src/StarSegmentation.h" in cmake
    assert "src/StarSegmentation.cpp" in cmake


def test_processing_settings_persist_new_alignment_parameters():
    app_config = read_source("AppConfig.cpp")
    settings = read_source("SettingsDialog.cpp")

    for token in (
        'QStringLiteral("otsuSigmaThreshold")',
        'QStringLiteral("otsuPeakFraction")',
        'QStringLiteral("connectivity")',
    ):
        assert token in app_config
    assert "otsuSigmaThreshold" in settings
    assert "otsuPeakFraction" in settings
    assert "connectivity" in settings


def test_settings_dialog_explicitly_binds_connectivity_combo_data():
    source = read_source("SettingsDialog.cpp")

    assert 'connectivity->addItem(QStringLiteral("4 连通"), 4);' in source
    assert 'connectivity->addItem(QStringLiteral("8 连通（默认）"), 8);' in source
