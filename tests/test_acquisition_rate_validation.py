from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"


def test_measurement_rate_accepts_positive_values_below_100_hz():
    app_config = (SRC / "AppConfig.cpp").read_text(encoding="utf-8")
    settings = (SRC / "SettingsDialog.cpp").read_text(encoding="utf-8")
    ui = (SRC / "SettingsDialog.ui").read_text(encoding="utf-8")

    assert "if (acquisition.measurementRateHz <= 0.0)" in app_config
    assert "测量采样率必须大于 0 Hz" in app_config
    assert "测量采样率必须至少为 100 Hz" not in app_config
    assert 'setSpin("acquisitionMeasurementRateSpin", a.measurementRateHz, 0.1,' in settings
    assert '<double>0.100000000000000</double>' in ui
