from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read_source(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8").replace('\\"', '"')


def test_deep_sea_theme_declares_instrument_visual_system():
    source = read_source("src/UiTheme.cpp")
    for token in (
        "#08131F",
        "#102536",
        "#14334A",
        "#2B5369",
        "#49D8E8",
        "#42D392",
        "#F6B85C",
        "#FF6675",
        'QFrame[panelRole="canvas"]',
        'QFrame[panelRole="result"]',
        'QFrame[panelRole="diagnostic"]',
        'QLabel[statusBadge="true"]',
        "QPushButton:focus",
        "QLineEdit:focus",
        "QPlainTextEdit",
        "QSplitter::handle:horizontal",
    ):
        assert token in source, f"missing visual token or selector: {token}"


def test_main_window_exposes_observation_and_diagnostic_hierarchy():
    source = read_source("src/MainWindow.cpp")
    for token in (
        "windowSubtitleLabel",
        "全画幅观测",
        'panelRole", "canvas"',
        'panelRole", "result"',
        'panelRole", "diagnostic"',
        'statusBadge", true',
        "setStretchFactor(0, 58)",
        "setStretchFactor(1, 42)",
        "运行日志",
    ):
        assert token in source, f"missing main-window hierarchy marker: {token}"


def test_settings_dialog_exposes_cards_and_usable_window_size():
    cpp = read_source("src/SettingsDialog.cpp")
    ui = read_source("src/SettingsDialog.ui")
    for token in (
        'settingsCard", true',
        "processingGrid->setContentsMargins",
        "processingGrid->setHorizontalSpacing",
        "setMinimumSize(760, 620)",
    ):
        assert token in cpp, f"missing settings visual marker: {token}"
    assert "<width>820</width>" in ui
    assert "<height>680</height>" in ui
