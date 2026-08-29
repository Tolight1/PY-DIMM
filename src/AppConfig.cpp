#include "AppConfig.h"
#include "ConnectedDomain.h"

#include <QSettings>

#include <cmath>

namespace {

constexpr int kAppConfigVersion = 2;
constexpr double legacySubApertureDiameterMm = 60.0;
constexpr double legacyBaselineSeparationMm = 150.0;
constexpr double legacyZenithAngleDeg = 0.0;
constexpr double kConfigValueEpsilon = 1.0e-9;

bool isLegacyDefault(double value, double legacyValue)
{
    return std::abs(value - legacyValue) <= kConfigValueEpsilon;
}

}

AppConfig AppConfig::defaults()
{
    AppConfig config;
    // Fixed initial values from the KY-DIMM plan. Every item marked configurable
    // is editable in the settings dialog and persisted through QSettings.
    config.optical = OpticalConfig();
    config.acquisition = AcquisitionConfig();
    config.processing = ProcessingConfig();
    config.trigger = TriggerConfig();
    config.storage = StorageConfig();
    config.ui = UiConfig();
    return config;
}

QStringList AppConfig::validate() const
{
    QStringList errors;

    if (optical.mainTelescopeApertureMm <= 0.0)
        errors << QStringLiteral("主镜口径必须大于 0 mm");
    if (optical.subApertureDiameterMm <= 0.0)
        errors << QStringLiteral("子孔径直径必须大于 0 mm");
    if (optical.baselineSeparationMm <= 0.0)
        errors << QStringLiteral("两个圆形窗口中心距离必须大于 0 mm");
    if (!std::isfinite(optical.baselineAngleDeg))
        errors << QStringLiteral("基线方向角必须是有限数值");
    if (optical.focalLengthMm <= 0.0)
        errors << QStringLiteral("望远镜焦距必须大于 0 mm");
    if (optical.wavelengthNm <= 0.0)
        errors << QStringLiteral("波长必须大于 0 nm");
    if (optical.pixelSizeUm <= 0.0)
        errors << QStringLiteral("像元尺寸必须大于 0 µm");

    if (acquisition.frameWidth != 1920 || acquisition.frameHeight != 1200)
        errors << QStringLiteral("当前版本仅允许 1920 × 1200 全画幅");
    if (acquisition.pixelFormat != PixelFormat::Mono8)
        errors << QStringLiteral("当前版本仅允许 Mono8");
    if (acquisition.measurementRateHz <= 0.0)
        errors << QStringLiteral("测量采样率必须大于 0 Hz");
    if (acquisition.previewRateHz <= 0.0)
        errors << QStringLiteral("预览刷新率必须大于 0 Hz");
    if (acquisition.exposureTimeMs <= 0.0 || acquisition.exposureTimeMs > 10.0)
        errors << QStringLiteral("单次曝光时间必须在 (0, 10] ms 范围内");
    if (acquisition.targetSampleCount <= 0 || acquisition.targetDurationSec <= 0.0)
        errors << QStringLiteral("目标采样数和目标时长必须大于 0");

    if (processing.otsuSigmaThreshold < 0.0 || processing.otsuSigmaThreshold > 20.0)
        errors << QStringLiteral("Otsu sigma threshold must be between 0 and 20");
    if (processing.otsuPeakFraction < 0.01 || processing.otsuPeakFraction > 0.95)
        errors << QStringLiteral("Otsu peak fraction must be between 0.01 and 0.95");
    if (!ConnectedDomain::isValidConnectivity(processing.connectivity))
        errors << QStringLiteral("Connectivity must be 4 or 8");
    if (processing.otsuMinimumComponentAreaPx < ConnectedDomain::kMinimumComponentArea ||
        processing.otsuMaximumComponentAreaPx < processing.otsuMinimumComponentAreaPx)
        errors << QStringLiteral("连通域面积范围无效");
    if (processing.centroidKernelRadiusPx < 1 ||
        processing.centroidKernelRadiusPx > 20)
        errors << QStringLiteral("质心计算核半径必须在 1 至 20 像素之间");
    if (processing.minimumPeakDistancePx <= 0.0)
        errors << QStringLiteral("候选最小间距基准必须大于 0 px");
    if (processing.maximumDifferentialJumpPx <= 0.0)
        errors << QStringLiteral("双星差分最大跳变量必须大于 0 px");
    if (processing.differentialBaselineViolationFrames < 1)
        errors << QStringLiteral("双星差分基线连续偏离帧数必须至少为 1");
    if (processing.r0WindowFrames < 2)
        errors << QStringLiteral("r0 计算窗口至少需要 2 个有效样本");
    if (processing.resultUpdateIntervalSec <= 0.0)
        errors << QStringLiteral("结果更新间隔必须大于 0 s");
    if (processing.tau0HistorySeconds <= 0 ||
        processing.tau0MaximumLagMs <= 0.0 ||
        processing.tau0MinimumSamples < 2)
        errors << QStringLiteral("tau0 参数无效");
    if (processing.hardwareAoiMarginPx < 0 ||
        processing.hardwareAoiMaxWidthPx < 1 ||
        processing.hardwareAoiMaxHeightPx < 1 ||
        processing.hardwareAoiUpdateDistanceToEdgePx < 0 ||
        processing.hardwareAoiUpdateMinimumShiftPx <= 0.0 ||
        processing.hardwareAoiUpdateCooldownMs < 0 ||
        processing.lostPairRelocalizationFrames < 1)
        errors << QStringLiteral("硬件 AOI 和双星跟踪参数无效");

    if (static_cast<int>(trigger.mode) <
            static_cast<int>(TriggerMode::Continuous) ||
        static_cast<int>(trigger.mode) >
            static_cast<int>(TriggerMode::Hardware))
        errors << QStringLiteral("触发模式无效");
    if (storage.resultRecordIntervalSec <= 0.0)
        errors << QStringLiteral("结果记录间隔必须大于 0 s");

    if (storage.outputDirectory.trimmed().isEmpty())
        errors << QStringLiteral("数据输出目录不能为空");
    if (!storage.imageSavingFixedOff)
        errors << QStringLiteral("当前版本不允许保存原始图像");

    return errors;
}

void AppConfig::save(QSettings &settings) const
{
    settings.setValue(QStringLiteral("configVersion"), kAppConfigVersion);

    settings.beginGroup(QStringLiteral("physical"));
    settings.setValue(QStringLiteral("mainTelescopeApertureMm"), optical.mainTelescopeApertureMm);
    settings.setValue(QStringLiteral("subApertureDiameterMm"), optical.subApertureDiameterMm);
    settings.setValue(QStringLiteral("baselineSeparationMm"), optical.baselineSeparationMm);
    settings.setValue(QStringLiteral("baselineAngleDeg"), optical.baselineAngleDeg);
    settings.setValue(QStringLiteral("focalLengthMm"), optical.focalLengthMm);
    settings.setValue(QStringLiteral("wavelengthNm"), optical.wavelengthNm);
    settings.setValue(QStringLiteral("pixelSizeUm"), optical.pixelSizeUm);
    settings.setValue(QStringLiteral("zenithAngleDeg"), optical.zenithAngleDeg);
    settings.endGroup();

    settings.beginGroup(QStringLiteral("acquisition"));
    settings.setValue(QStringLiteral("frameWidth"), acquisition.frameWidth);
    settings.setValue(QStringLiteral("frameHeight"), acquisition.frameHeight);
    settings.setValue(QStringLiteral("measurementRateHz"), acquisition.measurementRateHz);
    settings.setValue(QStringLiteral("previewRateHz"), acquisition.previewRateHz);
    settings.setValue(QStringLiteral("exposureTimeMs"), acquisition.exposureTimeMs);
    settings.setValue(QStringLiteral("targetSampleCount"), acquisition.targetSampleCount);
    settings.setValue(QStringLiteral("targetDurationSec"), acquisition.targetDurationSec);
    settings.setValue(QStringLiteral("enableHardwareAoi"), acquisition.enableHardwareAoi);
    settings.endGroup();

    settings.beginGroup(QStringLiteral("processing"));
    settings.setValue(QStringLiteral("hardwareAoiMarginPx"), processing.hardwareAoiMarginPx);
    settings.setValue(QStringLiteral("hardwareAoiMaxWidthPx"), processing.hardwareAoiMaxWidthPx);
    settings.setValue(QStringLiteral("hardwareAoiMaxHeightPx"), processing.hardwareAoiMaxHeightPx);
    settings.setValue(QStringLiteral("hardwareAoiUpdateDistanceToEdgePx"),
                      processing.hardwareAoiUpdateDistanceToEdgePx);
    settings.setValue(QStringLiteral("hardwareAoiUpdateMinimumShiftPx"),
                      processing.hardwareAoiUpdateMinimumShiftPx);
    settings.setValue(QStringLiteral("hardwareAoiUpdateCooldownMs"),
                      processing.hardwareAoiUpdateCooldownMs);
    settings.setValue(QStringLiteral("otsuSigmaThreshold"), processing.otsuSigmaThreshold);
    settings.setValue(QStringLiteral("otsuPeakFraction"), processing.otsuPeakFraction);
    settings.setValue(QStringLiteral("connectivity"), processing.connectivity);
    settings.setValue(QStringLiteral("otsuMinimumComponentAreaPx"), processing.otsuMinimumComponentAreaPx);
    settings.setValue(QStringLiteral("otsuMaximumComponentAreaPx"), processing.otsuMaximumComponentAreaPx);
    settings.setValue(QStringLiteral("centroidKernelRadiusPx"),
                      processing.centroidKernelRadiusPx);
    settings.setValue(QStringLiteral("minimumPeakDistancePx"), processing.minimumPeakDistancePx);
    settings.setValue(QStringLiteral("minimumCentroidIntensity"), processing.minimumCentroidIntensity);
    settings.setValue(QStringLiteral("maximumDifferentialJumpPx"),
                      processing.maximumDifferentialJumpPx);
    settings.setValue(QStringLiteral("differentialBaselineViolationFrames"),
                      processing.differentialBaselineViolationFrames);
    settings.setValue(QStringLiteral("lostPairRelocalizationFrames"), processing.lostPairRelocalizationFrames);
    settings.setValue(QStringLiteral("r0WindowFrames"), processing.r0WindowFrames);
    settings.setValue(QStringLiteral("resultUpdateIntervalSec"), processing.resultUpdateIntervalSec);
    settings.setValue(QStringLiteral("tau0HistorySeconds"), processing.tau0HistorySeconds);
    settings.setValue(QStringLiteral("tau0MaximumLagMs"), processing.tau0MaximumLagMs);
    settings.setValue(QStringLiteral("tau0MinimumSamples"), processing.tau0MinimumSamples);
    settings.endGroup();

    settings.beginGroup(QStringLiteral("trigger"));
    settings.setValue(QStringLiteral("mode"), static_cast<int>(trigger.mode));
    settings.setValue(QStringLiteral("hardwareTriggerLine"), trigger.hardwareTriggerLine);
    settings.setValue(QStringLiteral("triggerSelectorFrameStart"), trigger.triggerSelectorFrameStart);
    settings.setValue(QStringLiteral("softwareTriggerEachFrame"), trigger.softwareTriggerEachFrame);
    settings.setValue(QStringLiteral("allowPartialScan"), trigger.allowPartialScan);
    settings.endGroup();

    settings.beginGroup(QStringLiteral("storage"));
    settings.setValue(QStringLiteral("outputDirectory"), storage.outputDirectory);
    settings.setValue(QStringLiteral("resultRecordIntervalSec"), storage.resultRecordIntervalSec);
    settings.setValue(QStringLiteral("saveParameterCsv"), storage.saveParameterCsv);
    settings.setValue(QStringLiteral("saveCentroidCsv"), storage.saveCentroidCsv);
    settings.setValue(QStringLiteral("saveDiagnosticsCsv"), storage.saveDiagnosticsCsv);
    settings.setValue(QStringLiteral("saveRunMetadataJson"), storage.saveRunMetadataJson);
    settings.setValue(QStringLiteral("imageSavingFixedOff"), storage.imageSavingFixedOff);
    settings.endGroup();

    settings.beginGroup(QStringLiteral("ui"));
    settings.setValue(QStringLiteral("showFullFramePreview"), ui.showFullFramePreview);
    settings.setValue(QStringLiteral("drawHardwareAoi"), ui.drawHardwareAoi);
    settings.endGroup();
}

void AppConfig::load(QSettings &settings)
{
    AppConfig fallback = defaults();
    const int storedConfigVersion = settings.value(
        QStringLiteral("configVersion"), 0).toInt();

    settings.beginGroup(QStringLiteral("physical"));
    optical.mainTelescopeApertureMm = settings.value(QStringLiteral("mainTelescopeApertureMm"),
        fallback.optical.mainTelescopeApertureMm).toDouble();
    optical.subApertureDiameterMm = settings.value(QStringLiteral("subApertureDiameterMm"),
        fallback.optical.subApertureDiameterMm).toDouble();
    optical.baselineSeparationMm = settings.value(QStringLiteral("baselineSeparationMm"),
        fallback.optical.baselineSeparationMm).toDouble();
    optical.baselineAngleDeg = settings.value(QStringLiteral("baselineAngleDeg"),
        fallback.optical.baselineAngleDeg).toDouble();
    optical.focalLengthMm = settings.value(QStringLiteral("focalLengthMm"),
        fallback.optical.focalLengthMm).toDouble();
    optical.wavelengthNm = settings.value(QStringLiteral("wavelengthNm"),
        fallback.optical.wavelengthNm).toDouble();
    optical.pixelSizeUm = settings.value(QStringLiteral("pixelSizeUm"),
        fallback.optical.pixelSizeUm).toDouble();
    optical.zenithAngleDeg = settings.value(QStringLiteral("zenithAngleDeg"),
        fallback.optical.zenithAngleDeg).toDouble();
    settings.endGroup();

    // Version 1 stored the old physical defaults. Migrate only values that
    // still equal those defaults, so a user-customized value is preserved.
    if (storedConfigVersion < kAppConfigVersion) {
        settings.beginGroup(QStringLiteral("physical"));
        if (isLegacyDefault(optical.subApertureDiameterMm,
                            legacySubApertureDiameterMm)) {
            optical.subApertureDiameterMm = fallback.optical.subApertureDiameterMm;
            settings.setValue(QStringLiteral("subApertureDiameterMm"),
                              optical.subApertureDiameterMm);
        }
        if (isLegacyDefault(optical.baselineSeparationMm,
                            legacyBaselineSeparationMm)) {
            optical.baselineSeparationMm = fallback.optical.baselineSeparationMm;
            settings.setValue(QStringLiteral("baselineSeparationMm"),
                              optical.baselineSeparationMm);
        }
        if (isLegacyDefault(optical.zenithAngleDeg, legacyZenithAngleDeg)) {
            optical.zenithAngleDeg = fallback.optical.zenithAngleDeg;
            settings.setValue(QStringLiteral("zenithAngleDeg"),
                              optical.zenithAngleDeg);
        }
        settings.endGroup();
        settings.setValue(QStringLiteral("configVersion"), kAppConfigVersion);
        settings.sync();
    }

    settings.beginGroup(QStringLiteral("acquisition"));
    acquisition.frameWidth = settings.value(QStringLiteral("frameWidth"),
        fallback.acquisition.frameWidth).toInt();
    acquisition.frameHeight = settings.value(QStringLiteral("frameHeight"),
        fallback.acquisition.frameHeight).toInt();
    acquisition.measurementRateHz = settings.value(QStringLiteral("measurementRateHz"),
        fallback.acquisition.measurementRateHz).toDouble();
    acquisition.previewRateHz = settings.value(QStringLiteral("previewRateHz"),
        fallback.acquisition.previewRateHz).toDouble();
    acquisition.exposureTimeMs = settings.value(QStringLiteral("exposureTimeMs"),
        fallback.acquisition.exposureTimeMs).toDouble();
    acquisition.targetSampleCount = settings.value(QStringLiteral("targetSampleCount"),
        fallback.acquisition.targetSampleCount).toInt();
    acquisition.targetDurationSec = settings.value(QStringLiteral("targetDurationSec"),
        fallback.acquisition.targetDurationSec).toDouble();
    acquisition.enableHardwareAoi = settings.value(QStringLiteral("enableHardwareAoi"),
        fallback.acquisition.enableHardwareAoi).toBool();
    acquisition.pixelFormat = PixelFormat::Mono8;
    settings.endGroup();

    settings.beginGroup(QStringLiteral("processing"));
    processing.hardwareAoiMarginPx = settings.value(QStringLiteral("hardwareAoiMarginPx"),
        fallback.processing.hardwareAoiMarginPx).toInt();
    processing.hardwareAoiMaxWidthPx = settings.value(QStringLiteral("hardwareAoiMaxWidthPx"),
        fallback.processing.hardwareAoiMaxWidthPx).toInt();
    processing.hardwareAoiMaxHeightPx = settings.value(QStringLiteral("hardwareAoiMaxHeightPx"),
        fallback.processing.hardwareAoiMaxHeightPx).toInt();
    processing.hardwareAoiUpdateDistanceToEdgePx = settings.value(
        QStringLiteral("hardwareAoiUpdateDistanceToEdgePx"),
        fallback.processing.hardwareAoiUpdateDistanceToEdgePx).toInt();
    processing.hardwareAoiUpdateMinimumShiftPx = settings.value(
        QStringLiteral("hardwareAoiUpdateMinimumShiftPx"),
        fallback.processing.hardwareAoiUpdateMinimumShiftPx).toDouble();
    processing.hardwareAoiUpdateCooldownMs = settings.value(
        QStringLiteral("hardwareAoiUpdateCooldownMs"),
        fallback.processing.hardwareAoiUpdateCooldownMs).toInt();
    processing.otsuSigmaThreshold = settings.value(QStringLiteral("otsuSigmaThreshold"),
        fallback.processing.otsuSigmaThreshold).toDouble();
    processing.otsuPeakFraction = settings.value(QStringLiteral("otsuPeakFraction"),
        fallback.processing.otsuPeakFraction).toDouble();
    processing.connectivity = settings.value(QStringLiteral("connectivity"),
        fallback.processing.connectivity).toInt();
    processing.otsuMinimumComponentAreaPx = settings.value(QStringLiteral("otsuMinimumComponentAreaPx"),
        fallback.processing.otsuMinimumComponentAreaPx).toInt();
    processing.otsuMaximumComponentAreaPx = settings.value(QStringLiteral("otsuMaximumComponentAreaPx"),
        fallback.processing.otsuMaximumComponentAreaPx).toInt();
    processing.centroidKernelRadiusPx = settings.value(
        QStringLiteral("centroidKernelRadiusPx"),
        fallback.processing.centroidKernelRadiusPx).toInt();
    processing.minimumPeakDistancePx = settings.value(QStringLiteral("minimumPeakDistancePx"),
        fallback.processing.minimumPeakDistancePx).toDouble();
    processing.minimumCentroidIntensity = settings.value(QStringLiteral("minimumCentroidIntensity"),
        fallback.processing.minimumCentroidIntensity).toDouble();
    processing.maximumDifferentialJumpPx = settings.value(
        QStringLiteral("maximumDifferentialJumpPx"),
        fallback.processing.maximumDifferentialJumpPx).toDouble();
    processing.differentialBaselineViolationFrames = settings.value(
        QStringLiteral("differentialBaselineViolationFrames"),
        fallback.processing.differentialBaselineViolationFrames).toInt();
    processing.lostPairRelocalizationFrames = settings.value(QStringLiteral("lostPairRelocalizationFrames"),
        fallback.processing.lostPairRelocalizationFrames).toInt();
    processing.r0WindowFrames = settings.value(QStringLiteral("r0WindowFrames"),
        fallback.processing.r0WindowFrames).toInt();
    processing.resultUpdateIntervalSec = settings.value(QStringLiteral("resultUpdateIntervalSec"),
        fallback.processing.resultUpdateIntervalSec).toDouble();
    processing.tau0HistorySeconds = settings.value(QStringLiteral("tau0HistorySeconds"),
        fallback.processing.tau0HistorySeconds).toInt();
    processing.tau0MaximumLagMs = settings.value(QStringLiteral("tau0MaximumLagMs"),
        fallback.processing.tau0MaximumLagMs).toDouble();
    processing.tau0MinimumSamples = settings.value(QStringLiteral("tau0MinimumSamples"),
        fallback.processing.tau0MinimumSamples).toInt();
    settings.endGroup();

    settings.beginGroup(QStringLiteral("trigger"));
    trigger.mode = static_cast<TriggerMode>(settings.value(QStringLiteral("mode"),
        static_cast<int>(fallback.trigger.mode)).toInt());
    trigger.hardwareTriggerLine = settings.value(QStringLiteral("hardwareTriggerLine"),
        fallback.trigger.hardwareTriggerLine).toString();
    trigger.triggerSelectorFrameStart = settings.value(QStringLiteral("triggerSelectorFrameStart"),
        fallback.trigger.triggerSelectorFrameStart).toBool();
    trigger.softwareTriggerEachFrame = settings.value(QStringLiteral("softwareTriggerEachFrame"),
        fallback.trigger.softwareTriggerEachFrame).toBool();
    trigger.allowPartialScan = settings.value(QStringLiteral("allowPartialScan"),
        fallback.trigger.allowPartialScan).toBool();
    settings.endGroup();

    settings.beginGroup(QStringLiteral("storage"));
    storage.outputDirectory = settings.value(QStringLiteral("outputDirectory"),
        fallback.storage.outputDirectory).toString();
    storage.resultRecordIntervalSec = settings.value(QStringLiteral("resultRecordIntervalSec"),
        fallback.storage.resultRecordIntervalSec).toDouble();
    storage.saveParameterCsv = settings.value(QStringLiteral("saveParameterCsv"),
        fallback.storage.saveParameterCsv).toBool();
    storage.saveCentroidCsv = settings.value(QStringLiteral("saveCentroidCsv"),
        fallback.storage.saveCentroidCsv).toBool();
    storage.saveDiagnosticsCsv = settings.value(QStringLiteral("saveDiagnosticsCsv"),
        fallback.storage.saveDiagnosticsCsv).toBool();
    storage.saveRunMetadataJson = settings.value(QStringLiteral("saveRunMetadataJson"),
        fallback.storage.saveRunMetadataJson).toBool();
    storage.imageSavingFixedOff = settings.value(QStringLiteral("imageSavingFixedOff"),
        fallback.storage.imageSavingFixedOff).toBool();
    settings.endGroup();

    settings.beginGroup(QStringLiteral("ui"));
    ui.showFullFramePreview = settings.value(QStringLiteral("showFullFramePreview"),
        fallback.ui.showFullFramePreview).toBool();
    ui.drawHardwareAoi = settings.value(QStringLiteral("drawHardwareAoi"),
        fallback.ui.drawHardwareAoi).toBool();
    settings.endGroup();
}
