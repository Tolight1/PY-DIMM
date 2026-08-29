#include "UiTheme.h"

#include <QApplication>
#include <QColor>
#include <QPalette>

namespace UiTheme {

namespace {
constexpr const char *kBackground = "#08131F";
constexpr const char *kPanel = "#102536";
constexpr const char *kPanelAlt = "#14334A";
constexpr const char *kCanvas = "#050B12";
constexpr const char *kInput = "#091A29";
constexpr const char *kBorder = "#2B5369";
constexpr const char *kBorderStrong = "#3D718B";
constexpr const char *kText = "#E7F1F5";
constexpr const char *kTextDim = "#93AFC0";
constexpr const char *kCyan = "#49D8E8";
constexpr const char *kGreen = "#42D392";
constexpr const char *kWarning = "#F6B85C";
constexpr const char *kError = "#FF6675";
constexpr const char *kMuted = "#557284";
} // namespace

QString styleSheet()
{
    return QStringLiteral(
        "QMainWindow, QDialog { background: %1; }"
        "QWidget { color: %2; font-family: \"Segoe UI\", \"Microsoft YaHei UI\";"
        "          font-size: 10pt; }"
        "QLabel { background: transparent; }"
        "QLabel#windowTitleLabel { color: %3; font-size: 18pt; font-weight: 600; }"
        "QLabel#windowSubtitleLabel { color: %4; font-size: 9pt; }"
        "QLabel[sectionTitle=\"true\"] { color: %2; font-size: 11pt;"
        "          font-weight: 600; padding: 2px 0; }"
        "QLabel[sectionMeta=\"true\"] { color: %4; font-size: 9pt; }"
        "QLabel[cardTitle=\"true\"] { color: %4; font-size: 9pt; }"
        "QLabel[resultValue=\"true\"] { color: %3; font-family: \"Cascadia Mono\","
        "          \"Consolas\", monospace; font-size: 21pt; font-weight: 700; }"
        "QLabel[resultUnit=\"true\"] { color: %4; font-size: 9pt; }"
        "QLabel[statusBadge=\"true\"] { background: %7; border: 1px solid %8;"
        "          border-radius: 11px; padding: 4px 9px; color: %2; }"
        "QLabel[statusTone=\"success\"] { color: %5; border-color: %5; }"
        "QLabel[statusTone=\"warning\"] { color: %9; border-color: %9; }"
        "QLabel[statusTone=\"error\"] { color: %10; border-color: %10; }"
        "QLabel#measurementStateLabel { color: %3; font-weight: 600; }"
        "QFrame[panel=\"true\"] { background: %7; border: 1px solid %8;"
        "          border-radius: 8px; }"
        "QFrame[panelRole=\"canvas\"] { background: %6; border-color: %3; }"
        "QFrame[panelRole=\"result\"] { background: %11; border-color: %12; }"
        "QFrame[panelRole=\"diagnostic\"] { background: %11; border-color: %8; }"
        "QPushButton { background: %7; color: %2; border: 1px solid %8;"
        "          border-radius: 6px; padding: 7px 13px; min-height: 18px; }"
        "QPushButton:hover { background: %11; border-color: %3; }"
        "QPushButton:pressed { background: %6; }"
        "QPushButton:focus { border: 2px solid %3; padding: 6px 12px; }"
        "QPushButton:disabled { background: %13; color: %4; border-color: %13; }"
        "QPushButton#startAcquisitionButton { background: %5; color: #061B14;"
        "          border-color: %5; font-weight: 600; }"
        "QPushButton#startAcquisitionButton:hover { background: #65E6A8; }"
        "QPushButton#stopAcquisitionButton { background: %10; color: #2B060B;"
        "          border-color: %10; font-weight: 600; }"
        "QPushButton#stopAcquisitionButton:hover { background: #FF8792; }"
        "QTabWidget::pane { background: %7; border: 1px solid %8;"
        "          border-radius: 0 0 8px 8px; top: -1px; }"
        "QTabBar::tab { background: %7; color: %4; padding: 8px 14px;"
        "          border: 1px solid %8; border-bottom: none; }"
        "QTabBar::tab:hover { color: %2; background: %11; }"
        "QTabBar::tab:selected { background: %11; color: %3; border-color: %3; }"
        "QGroupBox { background: %7; border: 1px solid %8; border-radius: 8px;"
        "          margin-top: 12px; padding: 16px 12px 10px 12px; }"
        "QGroupBox[settingsCard=\"true\"] { background: %11; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 12px;"
        "          padding: 0 5px; color: %3; font-weight: 600; }"
        "QProgressBar { background: %6; border: 1px solid %8; border-radius: 5px;"
        "          text-align: center; color: %2; min-height: 14px; }"
        "QProgressBar::chunk { background: %3; border-radius: 4px; }"
        "QPlainTextEdit { background: %6; color: %2; border: 1px solid %8;"
        "          border-radius: 6px; padding: 6px; font-family: \"Cascadia Mono\","
        "          \"Consolas\", monospace; font-size: 9pt; }"
        "QPlainTextEdit:focus { border: 1px solid %3; }"
        "QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox { background: %6;"
        "          color: %2; border: 1px solid %8; border-radius: 5px;"
        "          padding: 4px 8px; min-height: 20px;"
        "          selection-background-color: %3; selection-color: %1; }"
        "QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus {"
        "          border: 2px solid %3; padding: 3px 7px; }"
        "QLineEdit:disabled, QSpinBox:disabled, QDoubleSpinBox:disabled,"
        "QComboBox:disabled { color: %4; background: %13; }"
        "QCheckBox::indicator { width: 15px; height: 15px; }"
        "QCheckBox::indicator:unchecked { background: %6; border: 1px solid %8;"
        "          border-radius: 3px; }"
        "QCheckBox::indicator:checked { background: %3; border: 1px solid %3;"
        "          border-radius: 3px; }"
        "QSplitter::handle:horizontal { width: 8px; background: %1; }"
        "QSplitter::handle:horizontal:hover { background: %3; }"
        "QScrollBar:vertical { background: %1; width: 10px; margin: 2px; }"
        "QScrollBar::handle:vertical { background: %8; min-height: 24px;"
        "          border-radius: 5px; }"
        "QScrollBar::handle:vertical:hover { background: %3; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
        "QStatusBar { background: %6; color: %4; border-top: 1px solid %8; }")
        .arg(QString::fromLatin1(kBackground), QString::fromLatin1(kText),
             QString::fromLatin1(kCyan), QString::fromLatin1(kTextDim),
             QString::fromLatin1(kGreen), QString::fromLatin1(kCanvas),
             QString::fromLatin1(kPanel), QString::fromLatin1(kBorder),
             QString::fromLatin1(kWarning), QString::fromLatin1(kError),
             QString::fromLatin1(kPanelAlt), QString::fromLatin1(kBorderStrong),
             QString::fromLatin1(kMuted));
}

void apply(QApplication &app)
{
    app.setStyle(QStringLiteral("Fusion"));

    QPalette palette;
    palette.setColor(QPalette::Window, QColor(QString::fromLatin1(kBackground)));
    palette.setColor(QPalette::WindowText, QColor(QString::fromLatin1(kText)));
    palette.setColor(QPalette::Base, QColor(QString::fromLatin1(kInput)));
    palette.setColor(QPalette::AlternateBase, QColor(QString::fromLatin1(kPanel)));
    palette.setColor(QPalette::ToolTipBase, QColor(QString::fromLatin1(kPanelAlt)));
    palette.setColor(QPalette::ToolTipText, QColor(QString::fromLatin1(kText)));
    palette.setColor(QPalette::Text, QColor(QString::fromLatin1(kText)));
    palette.setColor(QPalette::Button, QColor(QString::fromLatin1(kPanel)));
    palette.setColor(QPalette::ButtonText, QColor(QString::fromLatin1(kText)));
    palette.setColor(QPalette::BrightText, QColor(QString::fromLatin1(kCyan)));
    palette.setColor(QPalette::Link, QColor(QString::fromLatin1(kCyan)));
    palette.setColor(QPalette::Highlight, QColor(QString::fromLatin1(kCyan)));
    palette.setColor(QPalette::HighlightedText,
                     QColor(QString::fromLatin1(kBackground)));
    app.setPalette(palette);
    app.setStyleSheet(styleSheet());
}

} // namespace UiTheme
