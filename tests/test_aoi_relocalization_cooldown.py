from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read_source(name: str) -> str:
    return (ROOT / "src" / name).read_text(encoding="utf-8")


def test_full_frame_aoi_request_starts_the_same_cooldown_as_tracking_requests():
    source = read_source("TwoStarTracker.cpp")
    locate_block = source.split(
        "TrackerUpdate TwoStarTracker::locateFromFullFrame", 1
    )[1].split("TrackerUpdate TwoStarTracker::processAoiFrame", 1)[0]

    assert "update.requestHardwareAoi = true" in locate_block
    assert "lastHardwareAoiRequestMs_" in locate_block
    assert "QDateTime::currentMSecsSinceEpoch()" in locate_block


def test_tracking_request_still_obeys_the_configured_cooldown():
    source = read_source("TwoStarTracker.cpp")
    tracking_block = source.split(
        "TrackerUpdate TwoStarTracker::processAoiFrame", 1
    )[1]

    assert "nowMs - lastHardwareAoiRequestMs_ >=" in tracking_block
    assert "p.hardwareAoiUpdateCooldownMs" in tracking_block
