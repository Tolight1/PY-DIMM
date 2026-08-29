import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read_source(name: str) -> str:
    return (ROOT / name).read_text(encoding="utf-8")


def test_legacy_physical_defaults_are_migrated_without_overwriting_custom_values():
    source = read_source("src/AppConfig.cpp")

    assert "kAppConfigVersion" in source
    assert "legacySubApertureDiameterMm = 60.0" in source
    assert "legacyBaselineSeparationMm = 150.0" in source
    assert "legacyZenithAngleDeg = 0.0" in source
    assert re.search(
        r'settings\.value\(\s*QStringLiteral\("configVersion"\),\s*0\)\.toInt\(\)',
        source,
    )
    assert "settings.setValue(QStringLiteral(\"configVersion\"), kAppConfigVersion)" in source
    assert "isLegacyDefault(optical.subApertureDiameterMm," in source
    assert "isLegacyDefault(optical.baselineSeparationMm," in source
    assert "isLegacyDefault(optical.zenithAngleDeg, legacyZenithAngleDeg)" in source
