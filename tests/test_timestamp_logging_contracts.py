from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read_source(name: str) -> str:
    return (ROOT / "src" / name).read_text(encoding="utf-8")


def test_all_host_timestamp_csvs_add_iso_time_without_removing_numeric_time():
    source = read_source("ResultWriter.cpp")

    assert source.count('QStringLiteral("timestamp_s")') == 5
    assert source.count('QStringLiteral("timestamp_iso")') == 5
    assert "Qt::ISODateWithMs" in source


def test_camera_frame_clock_remains_numeric_and_separate_from_host_iso_time():
    source = read_source("ResultWriter.cpp")

    assert 'QStringLiteral("frame_timestamp_s")' in source
    assert "frame_timestamp_s" in source
    assert "timestamp_iso" in source
