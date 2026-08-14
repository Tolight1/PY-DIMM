from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def test_required_source_files_exist():
    required = [
        "CameraTypes.h", "ProcessingTypes.h", "AppConfig.cpp",
        "FrameQueue.cpp", "DisplayMailbox.cpp", "PylonCamera.cpp",
        "CameraWorker.cpp", "CentroidEngine.cpp", "TwoStarTracker.cpp",
        "AtmosphereCalculator.cpp", "MeasurementWorker.cpp",
        "ResultWriter.cpp", "SettingsDialog.cpp", "MainWindow.cpp",
    ]
    missing = [name for name in required if not (SRC / name).exists()]
    assert not missing, missing


def test_cmake_is_qt6_and_lists_kydimm():
    cmake = read(ROOT / "CMakeLists.txt")
    assert "project(KY_DIMM" in cmake
    assert "find_package(Qt6" in cmake
    assert "Qt5" not in cmake
    assert "MinGW" not in cmake
    assert "pylon" in cmake


def test_camera_boundary_isolated():
    for path in SRC.glob("*.cpp"):
        text = read(path)
        if path.name != "PylonCamera.cpp":
            assert "#include <pylon/" not in text
            assert "Pylon::" not in text


def test_forbidden_legacy_features_are_absent_from_source():
    text = "\n".join(read(path) for path in SRC.glob("*"))
    forbidden = [
        "saveFullFrameImages", "saveRoiImages", "darkField",
        "HotPixel", "GaussianFit", "IntensityCog", "Simulation",
    ]
    for token in forbidden:
        assert token not in text, token


def test_required_thread_separation_names_are_present():
    worker_text = read(SRC / "CameraWorker.h") + read(SRC / "MeasurementWorker.h")
    ui_text = read(SRC / "MainWindow.cpp")
    assert "FrameQueue" in worker_text
    assert "DisplayMailbox" in worker_text
    assert "QTimer" in ui_text
