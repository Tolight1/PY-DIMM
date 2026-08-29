#include "SettingsDialog.h"
#include "ui_SettingsDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QAbstractSpinBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFileDialog>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLayoutItem>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QSizePolicy>
#include <QSpinBox>

#include <QVector>

namespace {

template <typename T>
T *findWidget(QWidget *parent, const QString &objectName)
{
    return parent->findChild<T *>(objectName);
}

template <typename T>
T *findWidget(const QWidget *parent, const QString &objectName)
{
    return parent->findChild<T *>(objectName);
}

QString triggerModeName(TriggerMode mode)
{
    switch (mode) {
    case TriggerMode::Continuous:
        return QStringLiteral("连续采集");
    case TriggerMode::Software:
        return QStringLiteral("软件触发");
    case TriggerMode::Hardware:
        return QStringLiteral("硬件触发");
    }
    return QStringLiteral("连续采集");
}

} // namespace

SettingsDialog::SettingsDialog(const AppConfig &config, QWidget *parent)
    : QDialog(parent)
    , config_(config)
    , originalConfig_(config)
{
    Ui::SettingsDialog ui;
    ui.setupUi(this);
    // Bind the semantic values in code as well as in the .ui file.  This keeps
    // currentData() correct even when a stale AUTOUIC header is picked up by
    // an incremental build.
    if (QComboBox *connectivity =
            findWidget<QComboBox>(this, "processingConnectivityCombo")) {
        connectivity->clear();
        connectivity->addItem(QStringLiteral("4 连通"), 4);
        connectivity->addItem(QStringLiteral("8 连通（默认）"), 8);
    }
    setMinimumSize(760, 620);
    resize(820, 680);
    rebuildProcessingTab();

    const auto populateCombo = [](QComboBox *combo, const QStringList &items,
                                  const QString &current) {
        combo->clear();
        combo->addItems(items);
        combo->setCurrentIndex(combo->findText(current));
    };

    populateCombo(findWidget<QComboBox>(this, "triggerModeCombo"),
                  {QStringLiteral("连续采集"),
                   QStringLiteral("软件触发"),
                   QStringLiteral("硬件触发")},
                  triggerModeName(config.trigger.mode));
    populateCombo(findWidget<QComboBox>(this, "triggerHardwareLineCombo"),
                  {QStringLiteral("Line1"), QStringLiteral("Line2"),
                   QStringLiteral("Line3"), QStringLiteral("Line4")},
                  config.trigger.hardwareTriggerLine);

    // Acquisition page is read-only in this version: fixed 1920 × 1200 Mono8.
    if (QSpinBox *width = findWidget<QSpinBox>(this, "acquisitionWidthSpin")) {
        width->setReadOnly(true);
        width->setValue(config.acquisition.frameWidth);
    }
    if (QSpinBox *height = findWidget<QSpinBox>(this, "acquisitionHeightSpin")) {
        height->setReadOnly(true);
        height->setValue(config.acquisition.frameHeight);
    }
    if (QComboBox *format = findWidget<QComboBox>(this, "acquisitionPixelFormatCombo")) {
        format->clear();
        format->addItem(QStringLiteral("Mono8"));
        format->setCurrentIndex(0);
        format->setEnabled(false);
    }

    populateWidgets(config);
    installSpinWheelFilters();

    // Software trigger means the application issues one trigger per frame.
    if (QCheckBox *softwareEachFrame =
            findWidget<QCheckBox>(this, "triggerSoftwareEachFrameCheck")) {
        softwareEachFrame->setChecked(true);
        softwareEachFrame->setEnabled(false);
    }

    if (QPushButton *browse = findWidget<QPushButton>(this, "storageBrowseButton")) {
        connect(browse, &QPushButton::clicked, this, [this]() {
            QLineEdit *edit =
                findWidget<QLineEdit>(this, "storageOutputDirectoryEdit");
            const QString start =
                (edit && !edit->text().isEmpty())
                    ? edit->text()
                    : QDir::currentPath();
            const QString directory =
                QFileDialog::getExistingDirectory(this, tr("选择数据输出目录"),
                                                  start);
            if (!directory.isEmpty() && edit)
                edit->setText(QDir::toNativeSeparators(directory));
        });
    }


    if (QDialogButtonBox *buttons =
            findWidget<QDialogButtonBox>(this, "buttonBox")) {
        if (QPushButton *okButton = buttons->button(QDialogButtonBox::Ok))
            okButton->setText(tr("确定"));
        if (QPushButton *cancelButton = buttons->button(QDialogButtonBox::Cancel))
            cancelButton->setText(tr("取消"));
        QPushButton *applyButton =
            buttons->addButton(tr("应用"), QDialogButtonBox::ApplyRole);
        connect(applyButton, &QPushButton::clicked, this,
                &SettingsDialog::apply);
    }
}

bool SettingsDialog::eventFilter(QObject *watched, QEvent *event)
{
    QAbstractSpinBox *spin = qobject_cast<QAbstractSpinBox *>(watched);
    if (!spin) {
        QObject *parent = watched ? watched->parent() : nullptr;
        while (parent && !spin) {
            spin = qobject_cast<QAbstractSpinBox *>(parent);
            parent = parent->parent();
        }
    }
    if (spin && event->type() == QEvent::Wheel) {
        event->accept();
        return true;
    }
    return QDialog::eventFilter(watched, event);
}

void SettingsDialog::installSpinWheelFilters()
{
    const QList<QAbstractSpinBox *> spins =
        findChildren<QAbstractSpinBox *>();
    for (QAbstractSpinBox *spin : spins) {
        spin->installEventFilter(this);
        if (QLineEdit *lineEdit = spin->findChild<QLineEdit *>())
            lineEdit->installEventFilter(this);
        spin->setFocusPolicy(Qt::StrongFocus);
    }
}

void SettingsDialog::rebuildProcessingTab()
{
    QWidget *processingTab = findWidget<QWidget>(this, "processingTab");
    QGridLayout *processingGrid =
        findWidget<QGridLayout>(this, "processingGrid");
    if (!processingTab || !processingGrid)
        return;

    while (QLayoutItem *item = processingGrid->takeAt(0))
        delete item;

    processingGrid->setContentsMargins(4, 4, 4, 4);
    processingGrid->setHorizontalSpacing(12);
    processingGrid->setVerticalSpacing(12);

    const auto addGroup = [&](const QString &title,
                              const QVector<QPair<QString, QString>> &rows,
                              int row) {
        auto *group = new QGroupBox(title, processingTab);
        group->setProperty("settingsCard", true);
        auto *layout = new QGridLayout(group);
        layout->setContentsMargins(12, 16, 12, 10);
        layout->setHorizontalSpacing(10);
        layout->setVerticalSpacing(8);
        layout->setColumnStretch(1, 1);
        group->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
        int groupRow = 0;
        for (const auto &pair : rows) {
            QLabel *label = findWidget<QLabel>(this, pair.first);
            QWidget *editor = findWidget<QWidget>(this, pair.second);
            if (!label || !editor)
                continue;
            layout->addWidget(label, groupRow, 0);
            layout->addWidget(editor, groupRow, 1);
            ++groupRow;
        }
        processingGrid->addWidget(group, row / 2, row % 2, 1, 1);
    };

    addGroup(tr("Otsu 与候选"),
             {{QStringLiteral("labelProcessingOtsuSigma"),
               QStringLiteral("processingOtsuSigmaSpin")},
              {QStringLiteral("labelProcessingOtsuPeakFraction"),
               QStringLiteral("processingOtsuPeakFractionSpin")},
              {QStringLiteral("labelProcessingConnectivity"),
               QStringLiteral("processingConnectivityCombo")},
               {QStringLiteral("labelProcessingMinComponentArea"),
                QStringLiteral("processingMinComponentAreaSpin")},
               {QStringLiteral("labelProcessingMaxComponentArea"),
                QStringLiteral("processingMaxComponentAreaSpin")},
               {QStringLiteral("labelProcessingMinimumCentroidIntensity"),
                QStringLiteral("processingMinimumCentroidIntensitySpin")}},
             0);
    addGroup(tr("大气参数计算"),
             {{QStringLiteral("labelProcessingR0WindowFrames"),
               QStringLiteral("processingR0WindowFramesSpin")},
              {QStringLiteral("labelProcessingResultUpdateInterval"),
               QStringLiteral("processingResultUpdateIntervalSpin")},
              {QStringLiteral("labelProcessingTauHistorySeconds"),
               QStringLiteral("processingTauHistorySecondsSpin")},
              {QStringLiteral("labelProcessingTauMaximumLag"),
               QStringLiteral("processingTauMaximumLagMsSpin")},
              {QStringLiteral("labelProcessingTauMinimumSamples"),
               QStringLiteral("processingTauMinimumSamplesSpin")}},
             1);

    if (QLabel *help = findWidget<QLabel>(this, "labelProcessingHelp"))
        processingGrid->addWidget(help, 2, 0, 1, 2);
    processingGrid->setColumnStretch(0, 1);
    processingGrid->setColumnStretch(1, 1);
}

AppConfig SettingsDialog::currentConfig() const
{
    return config_;
}

void SettingsDialog::apply()
{
    const AppConfig candidate = readWidgets();
    const QStringList errors = candidate.validate();
    if (!errors.isEmpty()) {
        QMessageBox::warning(this, tr("参数无效"), errors.join(QLatin1Char('\n')));
        return;
    }

    config_ = candidate;
    emit configApplied(config_);
    acceptedConfig_ = true;
    writeSettings(config_);
}

void SettingsDialog::accept()
{
    const AppConfig candidate = readWidgets();
    const QStringList errors = candidate.validate();
    if (!errors.isEmpty()) {
        QMessageBox::warning(this, tr("参数无效"), errors.join(QLatin1Char('\n')));
        return;
    }

    config_ = candidate;
    emit configApplied(config_);
    acceptedConfig_ = true;
    writeSettings(config_);
    QDialog::accept();
}

void SettingsDialog::reject()
{
    // Cancel restores the pre-dialog snapshot and emits nothing.
    config_ = originalConfig_;
    QDialog::reject();
}

AppConfig SettingsDialog::readWidgets() const
{
    // Preserve settings that are not represented by a visible widget (for
    // example UI display flags) instead of resetting them on Apply.
    AppConfig config = config_;

    auto spin = [this](const QString &name) -> QDoubleSpinBox * {
        return findWidget<QDoubleSpinBox>(this, name);
    };
    auto ispin = [this](const QString &name) -> QSpinBox * {
        return findWidget<QSpinBox>(this, name);
    };
    auto combo = [this](const QString &name) -> QComboBox * {
        return findWidget<QComboBox>(this, name);
    };
    auto check = [this](const QString &name) -> QCheckBox * {
        return findWidget<QCheckBox>(this, name);
    };

    if (QDoubleSpinBox *w = spin("physicalMainApertureMmSpin"))
        config.optical.mainTelescopeApertureMm = w->value();
    if (QDoubleSpinBox *w = spin("physicalSubApertureMmSpin"))
        config.optical.subApertureDiameterMm = w->value();
    if (QDoubleSpinBox *w = spin("physicalBaselineMmSpin"))
        config.optical.baselineSeparationMm = w->value();
    if (QDoubleSpinBox *w = spin("physicalBaselineAngleDegSpin"))
        config.optical.baselineAngleDeg = w->value();
    if (QDoubleSpinBox *w = spin("physicalFocalLengthMmSpin"))
        config.optical.focalLengthMm = w->value();
    if (QDoubleSpinBox *w = spin("physicalWavelengthNmSpin"))
        config.optical.wavelengthNm = w->value();
    if (QDoubleSpinBox *w = spin("physicalPixelSizeUmSpin"))
        config.optical.pixelSizeUm = w->value();
    if (QDoubleSpinBox *w = spin("physicalZenithAngleDegSpin"))
        config.optical.zenithAngleDeg = w->value();

    if (QSpinBox *w = ispin("acquisitionWidthSpin"))
        config.acquisition.frameWidth = w->value();
    if (QSpinBox *w = ispin("acquisitionHeightSpin"))
        config.acquisition.frameHeight = w->value();
    if (QDoubleSpinBox *w = spin("acquisitionMeasurementRateSpin"))
        config.acquisition.measurementRateHz = w->value();
    if (QDoubleSpinBox *w = spin("acquisitionPreviewRateSpin"))
        config.acquisition.previewRateHz = w->value();
    if (QDoubleSpinBox *w = spin("acquisitionExposureMsSpin"))
        config.acquisition.exposureTimeMs = w->value();
    if (QSpinBox *w = ispin("acquisitionTargetSamplesSpin"))
        config.acquisition.targetSampleCount = w->value();
    if (QDoubleSpinBox *w = spin("acquisitionTargetDurationSecSpin"))
        config.acquisition.targetDurationSec = w->value();
    if (QCheckBox *c = check("acquisitionHardwareAoiCheck"))
        config.acquisition.enableHardwareAoi = c->isChecked();

    if (QSpinBox *w = ispin("aoiCentroidKernelRadiusSpin"))
        config.processing.centroidKernelRadiusPx = w->value();
    if (QSpinBox *w = ispin("aoiHardwareMarginSpin"))
        config.processing.hardwareAoiMarginPx = w->value();
    if (QSpinBox *w = ispin("aoiMaxWidthSpin"))
        config.processing.hardwareAoiMaxWidthPx = w->value();
    if (QSpinBox *w = ispin("aoiMaxHeightSpin"))
        config.processing.hardwareAoiMaxHeightPx = w->value();
    if (QSpinBox *w = ispin("aoiHardwareUpdateDistanceSpin"))
        config.processing.hardwareAoiUpdateDistanceToEdgePx = w->value();
    if (QDoubleSpinBox *w = spin("aoiHardwareUpdateShiftSpin"))
        config.processing.hardwareAoiUpdateMinimumShiftPx = w->value();
    if (QSpinBox *w = ispin("aoiHardwareUpdateCooldownSpin"))
        config.processing.hardwareAoiUpdateCooldownMs = w->value();
    if (QDoubleSpinBox *w = spin("aoiMinimumPeakDistanceSpin"))
        config.processing.minimumPeakDistancePx = w->value();
    if (QDoubleSpinBox *w = spin("aoiMaximumDifferentialJumpSpin"))
        config.processing.maximumDifferentialJumpPx = w->value();
    if (QSpinBox *w = ispin("aoiDifferentialBaselineViolationFramesSpin"))
        config.processing.differentialBaselineViolationFrames = w->value();
    if (QDoubleSpinBox *w = spin("processingOtsuSigmaSpin"))
        config.processing.otsuSigmaThreshold = w->value();
    if (QDoubleSpinBox *w = spin("processingOtsuPeakFractionSpin"))
        config.processing.otsuPeakFraction = w->value();
    if (QComboBox *w = combo("processingConnectivityCombo"))
        config.processing.connectivity = w->currentData().toInt();
    if (QSpinBox *w = ispin("processingMinComponentAreaSpin"))
        config.processing.otsuMinimumComponentAreaPx = w->value();
    if (QSpinBox *w = ispin("processingMaxComponentAreaSpin"))
        config.processing.otsuMaximumComponentAreaPx = w->value();
    if (QDoubleSpinBox *w = spin("processingMinimumCentroidIntensitySpin"))
        config.processing.minimumCentroidIntensity = w->value();
    if (QSpinBox *w = ispin("aoiLostPairRelocalizationFramesSpin"))
        config.processing.lostPairRelocalizationFrames = w->value();
    if (QSpinBox *w = ispin("processingR0WindowFramesSpin"))
        config.processing.r0WindowFrames = w->value();
    if (QDoubleSpinBox *w = spin("processingResultUpdateIntervalSpin"))
        config.processing.resultUpdateIntervalSec = w->value();
    if (QSpinBox *w = ispin("processingTauHistorySecondsSpin"))
        config.processing.tau0HistorySeconds = w->value();
    if (QDoubleSpinBox *w = spin("processingTauMaximumLagMsSpin"))
        config.processing.tau0MaximumLagMs = w->value();
    if (QSpinBox *w = ispin("processingTauMinimumSamplesSpin"))
        config.processing.tau0MinimumSamples = w->value();

    if (QComboBox *c = combo("triggerModeCombo")) {
        switch (c->currentIndex()) {
        case 0:
            config.trigger.mode = TriggerMode::Continuous;
            break;
        case 1:
            config.trigger.mode = TriggerMode::Software;
            break;
        case 2:
            config.trigger.mode = TriggerMode::Hardware;
            break;
        default:
            config.trigger.mode = TriggerMode::Continuous;
            break;
        }
    }
    if (QComboBox *c = combo("triggerHardwareLineCombo"))
        config.trigger.hardwareTriggerLine = c->currentText();
    if (QCheckBox *c = check("triggerFrameStartCheck"))
        config.trigger.triggerSelectorFrameStart = c->isChecked();
    if (QCheckBox *c = check("triggerSoftwareEachFrameCheck"))
        config.trigger.softwareTriggerEachFrame = c->isChecked();
    if (QCheckBox *c = check("triggerAllowPartialScanCheck"))
        config.trigger.allowPartialScan = c->isChecked();

    if (QLineEdit *edit = findWidget<QLineEdit>(this, "storageOutputDirectoryEdit"))
        config.storage.outputDirectory = edit->text();
    if (QDoubleSpinBox *w = spin("storageRecordIntervalSpin"))
        config.storage.resultRecordIntervalSec = w->value();
    if (QCheckBox *c = check("storageParameterCsvCheck"))
        config.storage.saveParameterCsv = c->isChecked();
    if (QCheckBox *c = check("storageCentroidCsvCheck"))
        config.storage.saveCentroidCsv = c->isChecked();
    if (QCheckBox *c = check("storageDiagnosticsCsvCheck"))
        config.storage.saveDiagnosticsCsv = c->isChecked();
    if (QCheckBox *c = check("storageMetadataJsonCheck"))
        config.storage.saveRunMetadataJson = c->isChecked();

    // imageSavingFixedOff stays true; there is no image-save control in this
    // version.
    config.storage.imageSavingFixedOff = true;

    return config;
}

void SettingsDialog::populateWidgets(const AppConfig &config)
{
    auto spin = [this](const QString &name) -> QDoubleSpinBox * {
        return findWidget<QDoubleSpinBox>(this, name);
    };
    auto ispin = [this](const QString &name) -> QSpinBox * {
        return findWidget<QSpinBox>(this, name);
    };
    auto combo = [this](const QString &name) -> QComboBox * {
        return findWidget<QComboBox>(this, name);
    };
    auto check = [this](const QString &name) -> QCheckBox * {
        return findWidget<QCheckBox>(this, name);
    };

    auto setSpin = [&spin](const QString &name, double value,
                           double minimum, double maximum, int decimals) {
        if (QDoubleSpinBox *w = spin(name)) {
            w->setRange(minimum, maximum);
            w->setDecimals(decimals);
            w->setValue(value);
        }
    };
    auto setISpin = [&ispin](const QString &name, int value,
                             int minimum, int maximum) {
        if (QSpinBox *w = ispin(name)) {
            w->setRange(minimum, maximum);
            w->setValue(value);
        }
    };
    auto setCheck = [&check](const QString &name, bool value) {
        if (QCheckBox *c = check(name))
            c->setChecked(value);
    };

    const OpticalConfig &o = config.optical;
    setSpin("physicalMainApertureMmSpin", o.mainTelescopeApertureMm, 1.0, 2000.0, 1);
    setSpin("physicalSubApertureMmSpin", o.subApertureDiameterMm, 1.0, 500.0, 1);
    setSpin("physicalBaselineMmSpin", o.baselineSeparationMm, 1.0, 500.0, 1);
    setSpin("physicalBaselineAngleDegSpin", o.baselineAngleDeg, -180.0, 180.0, 2);
    setSpin("physicalFocalLengthMmSpin", o.focalLengthMm, 10.0, 20000.0, 1);
    setSpin("physicalWavelengthNmSpin", o.wavelengthNm, 300.0, 1100.0, 1);
    setSpin("physicalPixelSizeUmSpin", o.pixelSizeUm, 0.5, 20.0, 3);
    setSpin("physicalZenithAngleDegSpin", o.zenithAngleDeg, 0.0, 90.0, 2);

    const AcquisitionConfig &a = config.acquisition;
    setISpin("acquisitionWidthSpin", a.frameWidth, 1, 10000);
    setISpin("acquisitionHeightSpin", a.frameHeight, 1, 10000);
    setSpin("acquisitionMeasurementRateSpin", a.measurementRateHz, 0.1, 10000.0, 1);
    setSpin("acquisitionPreviewRateSpin", a.previewRateHz, 0.1, 120.0, 1);
    setSpin("acquisitionExposureMsSpin", a.exposureTimeMs, 0.01, 10.0, 3);
    setISpin("acquisitionTargetSamplesSpin", a.targetSampleCount, 100, 1000000);
    setSpin("acquisitionTargetDurationSecSpin", a.targetDurationSec, 1.0, 3600.0, 1);
    setCheck("acquisitionHardwareAoiCheck", a.enableHardwareAoi);

    const ProcessingConfig &p = config.processing;
    setISpin("aoiCentroidKernelRadiusSpin", p.centroidKernelRadiusPx, 1, 20);
    setISpin("aoiHardwareMarginSpin", p.hardwareAoiMarginPx, 0, 1024);
    setISpin("aoiMaxWidthSpin", p.hardwareAoiMaxWidthPx, 1, 1920);
    setISpin("aoiMaxHeightSpin", p.hardwareAoiMaxHeightPx, 1, 1200);
    setISpin("aoiHardwareUpdateDistanceSpin",
             p.hardwareAoiUpdateDistanceToEdgePx, 0, 1024);
    setSpin("aoiHardwareUpdateShiftSpin",
            p.hardwareAoiUpdateMinimumShiftPx, 0.5, 2000.0, 1);
    setISpin("aoiHardwareUpdateCooldownSpin",
             p.hardwareAoiUpdateCooldownMs, 0, 600000);
    setSpin("aoiMinimumPeakDistanceSpin", p.minimumPeakDistancePx, 1.0, 500.0, 1);
    setSpin("aoiMaximumDifferentialJumpSpin",
            p.maximumDifferentialJumpPx, 0.5, 2000.0, 1);
    setISpin("aoiDifferentialBaselineViolationFramesSpin",
             p.differentialBaselineViolationFrames, 1, 10000);
    setSpin("processingOtsuSigmaSpin", p.otsuSigmaThreshold, 0.0, 20.0, 2);
    setSpin("processingOtsuPeakFractionSpin", p.otsuPeakFraction, 0.01, 0.95, 2);
    if (QComboBox *w = combo("processingConnectivityCombo")) {
        const int index = w->findData(p.connectivity);
        w->setCurrentIndex(index >= 0 ? index : w->findData(8));
    }
    setISpin("processingMinComponentAreaSpin", p.otsuMinimumComponentAreaPx, 9, 4096);
    setISpin("processingMaxComponentAreaSpin", p.otsuMaximumComponentAreaPx, 2, 262144);
    setSpin("processingMinimumCentroidIntensitySpin",
            p.minimumCentroidIntensity, 0.0, 65535.0, 2);
    setISpin("aoiLostPairRelocalizationFramesSpin",
             p.lostPairRelocalizationFrames, 1, 10000);
    setISpin("processingR0WindowFramesSpin", p.r0WindowFrames, 10, 100000);
    setSpin("processingResultUpdateIntervalSpin", p.resultUpdateIntervalSec, 0.05, 600.0, 2);
    setISpin("processingTauHistorySecondsSpin", p.tau0HistorySeconds, 1, 600);
    setSpin("processingTauMaximumLagMsSpin", p.tau0MaximumLagMs, 1.0, 5000.0, 1);
    setISpin("processingTauMinimumSamplesSpin", p.tau0MinimumSamples, 2, 10000);

    const TriggerConfig &t = config.trigger;
    setCheck("triggerFrameStartCheck", t.triggerSelectorFrameStart);
    setCheck("triggerSoftwareEachFrameCheck", t.softwareTriggerEachFrame);
    setCheck("triggerAllowPartialScanCheck", t.allowPartialScan);

    const StorageConfig &s = config.storage;
    if (QLineEdit *edit = findWidget<QLineEdit>(this, "storageOutputDirectoryEdit"))
        edit->setText(s.outputDirectory);
    setSpin("storageRecordIntervalSpin", s.resultRecordIntervalSec, 0.05, 600.0, 2);
    setCheck("storageParameterCsvCheck", s.saveParameterCsv);
    setCheck("storageCentroidCsvCheck", s.saveCentroidCsv);
    setCheck("storageDiagnosticsCsvCheck", s.saveDiagnosticsCsv);
    setCheck("storageMetadataJsonCheck", s.saveRunMetadataJson);
}

void SettingsDialog::writeSettings(const AppConfig &config) const
{
    QSettings settings;
    config.save(settings);
    settings.sync();
}
