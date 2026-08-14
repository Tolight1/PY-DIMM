from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"


def read_source(name: str) -> str:
    return (SRC / name).read_text(encoding="utf-8")


def test_main_window_uses_consistent_copy():
    text = read_source("MainWindow.cpp")
    expected = [
        "相机：未连接",
        "采集状态：已停止",
        "断开相机",
        "停止采集",
        "全画幅 1920 × 1200",
        "ROI A 64 × 64",
        "ROI 跟踪：未开始",
        "相机丢帧：%1",
        "队列丢帧：%1",
    ]
    forbidden = [
        "相机: 未连接",
        "断开连接",
        'tr("停止")',
        "ROI跟踪",
        "有效帧:",
    ]
    for phrase in expected:
        assert phrase in text, phrase
    for phrase in forbidden:
        assert phrase not in text, phrase


def test_settings_dialog_uses_consistent_copy():
    ui = (SRC / "SettingsDialog.ui").read_text(encoding="utf-8")
    source = read_source("SettingsDialog.cpp")
    expected_ui = [
        "主镜口径（mm）",
        "两个圆形窗口中心距离（mm）",
        "默认值：D = 60 mm、B = 150 mm（物理基线）、f = 2500 mm、λ = 550 nm、像元尺寸 = 5.86 µm。",
        "处理路径：原生 Otsu + 均值/4σ/峰值比例阈值 → 可配置连通域（默认 8 连通、面积 9–1000）→ 7×7 小核强度加权质心。",
        "软件触发模式下，每帧发送一次触发信号",
        "浏览…",
    ]
    expected_source = [
        'tr("ROI 重定位")',
        'tr("选择数据输出目录")',
        'tr("确定")',
        'tr("取消")',
    ]
    forbidden = [
        "主镜口径 (mm)",
        "重定中心",
        "浏览...",
        "一帧帧起始",
    ]
    for phrase in expected_ui:
        assert phrase in ui, phrase
    for phrase in expected_source:
        assert phrase in source, phrase
    for phrase in forbidden:
        assert phrase not in ui and phrase not in source, phrase


def test_runtime_copy_uses_consistent_terms():
    app_config = read_source("AppConfig.cpp")
    atmosphere = read_source("AtmosphereCalculator.cpp")
    tracker = read_source("TwoStarTracker.cpp")
    expected = [
        "子孔径直径必须大于 0 mm",
        "像元尺寸必须大于 0 µm",
        "当前版本仅允许 1920 × 1200 全画幅",
        "软件 ROI 至少为 16 × 16 像素",
        "等待足够的有效样本（%1 / %2）",
        "经过亮度/形状一致性过滤后，候选目标不足",
        "未找到满足最小间距要求的双星对",
    ]
    forbidden = [
        "子瞳直径必须大于 0 mm",
        "像元尺寸必须大于 0 um",
        "1920 x 1200",
        "16 x 16",
        "等待足够有效帧",
        "重定中心",
    ]
    combined = app_config + atmosphere + tracker
    for phrase in expected:
        assert phrase in combined, phrase
    for phrase in forbidden:
        assert phrase not in combined, phrase


def test_legacy_window_and_project_document_are_clean_utf8():
    legacy_ui = (SRC / "UI_New.ui").read_text(encoding="utf-8")
    document_path = ROOT / "KY-DIMM-项目介绍.md"
    document = document_path.read_bytes().decode("utf-8")

    assert "<string>KY-DIMM</string>" in legacy_ui
    assert "# KY-DIMM 项目介绍" in document
    assert "窗口中心距离" in document
    assert "ROI 跟踪" in document
    assert "µm" in document
    assert "椤圭洰浠嬬粛" not in document
    assert "ROI跟踪" not in document
    assert "重定中心" not in document
