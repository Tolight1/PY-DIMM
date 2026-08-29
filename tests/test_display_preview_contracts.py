from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"


def read_source(name: str) -> str:
    return (SRC / name).read_text(encoding="utf-8")


def test_aoi_frames_patch_the_latest_full_frame_preview():
    mailbox_header = read_source("DisplayMailbox.h")
    mailbox_source = read_source("DisplayMailbox.cpp")
    measurement_worker = read_source("MeasurementWorker.cpp")

    assert "publishAoiPatch" in mailbox_header
    assert "publishAoiPatch" in mailbox_source
    assert "sourceRect" in mailbox_source
    assert "fullFrameSize" in mailbox_source
    assert "copyTo" in mailbox_source
    assert "displayMailbox_.publishAoiPatch" in measurement_worker
    assert "frame.mono8" in measurement_worker
    assert "frame.sourceRect" in measurement_worker


def test_full_frame_camera_publish_remains_available_for_relocalization():
    camera_worker = read_source("CameraWorker.cpp")

    assert "const bool isFullFrame" in camera_worker
    assert "snap.fullFrameMono8 = frame.mono8.clone();" in camera_worker
    assert "displayMailbox_.publish(std::move(snap));" in camera_worker
    assert "measurementQueue_.push(std::move(frame));" in camera_worker


def test_preview_text_explains_aoi_live_region_and_stale_outside_region():
    main_window = read_source("MainWindow.cpp")

    assert "AOI区域实时" in main_window
    assert "区域外保持最近全画幅" in main_window


def test_preview_is_single_full_frame_stream_without_roi_cards():
    main_window = read_source("MainWindow.cpp")
    worker = read_source("MeasurementWorker.cpp")
    types = read_source("ProcessingTypes.h")

    assert "previewRateHz" in main_window
    assert "previewRateHz" in worker
    assert "refreshRoiPreview" not in main_window
    assert "roiAMono8" not in worker
    assert "roiBMono8" not in worker
    assert "showRoiPreview" not in types
    assert "drawSoftwareRois" not in types


def test_aoi_generation_change_publishes_overlay_state_immediately():
    worker = read_source("MeasurementWorker.cpp")

    assert "publishOverlayState" in worker
    assert "publishOverlayState();" in worker
    assert "publishOverlayState(frame.sequence);" in worker
    assert "snapshot.overlay = tracker_.currentOverlay();" in worker


def test_full_frame_preview_supports_user_zoom_and_fit_reset():
    main_window = read_source("MainWindow.cpp")

    assert "class ZoomableImageView" in main_window
    assert "wheelEvent" in main_window
    assert "fitToWindow" in main_window
    assert "fullFrameImageView_" in main_window
    assert "setScaledContents(true)" not in main_window


def test_valid_centroid_state_is_published_even_when_preview_is_throttled():
    worker = read_source("MeasurementWorker.cpp")

    assert "publishOverlayState(frame.sequence);" in worker
    assert worker.count("publishOverlayState(frame.sequence);") >= 3


def test_centroid_crosses_are_small_but_visible_on_bright_stars():
    adapter = read_source("ImageDisplayAdapter.cpp")

    assert "QColor(0, 0, 0, 220)" in adapter
    assert "const double arm = 8.0" in adapter
    assert "centroidPen.setWidthF(2.0)" in adapter
