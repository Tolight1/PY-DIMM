#include "ResultWriter.h"

#include <QDateTime>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTextStream>
#include <QtGlobal>

#include <cmath>

namespace {

// Minimal CSV field escaping: quote when a field contains a comma, quote, or
// newline; double any embedded quotes.
QString csvField(const QString &value)
{
    if (!value.contains(QLatin1Char(',')) &&
        !value.contains(QLatin1Char('"')) &&
        !value.contains(QLatin1Char('\n')) &&
        !value.contains(QLatin1Char('\r')))
        return value;
    QString escaped = value;
    escaped.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    return QStringLiteral("\"%1\"").arg(escaped);
}

QString csvLine(const QStringList &fields)
{
    QStringList escaped;
    escaped.reserve(fields.size());
    for (const QString &field : fields)
        escaped.append(csvField(field));
    return escaped.join(QLatin1Char(','));
}

double nowTimestampSec()
{
    return QDateTime::currentMSecsSinceEpoch() / 1000.0;
}

QString pixelFormatName(PixelFormat format)
{
    switch (format) {
    case PixelFormat::Mono8:
        return QStringLiteral("Mono8");
    case PixelFormat::Unknown:
        break;
    }
    return QStringLiteral("Unknown");
}

QString triggerModeName(TriggerMode mode)
{
    switch (mode) {
    case TriggerMode::Continuous:
        return QStringLiteral("Continuous");
    case TriggerMode::Software:
        return QStringLiteral("Software");
    case TriggerMode::Hardware:
        return QStringLiteral("Hardware");
    }
    return QStringLiteral("Unknown");
}

} // namespace

ResultWriter::~ResultWriter()
{
    finishRun();
}

bool ResultWriter::startRun(const AppConfig &config,
                            const CameraCapabilities &capabilities,
                            QString *error)
{
    finishRun();

    const QString baseDirectory = config.storage.outputDirectory;
    if (baseDirectory.trimmed().isEmpty()) {
        if (error)
            *error = QStringLiteral("输出目录为空，无法创建结果目录");
        finishRun();
        return false;
    }

    resultRecordIntervalSec_ = config.storage.resultRecordIntervalSec;
    lastResultRecordMs_ = -1;

    const QString timeStamp =
        QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"));
    runDirectory_ = QDir(baseDirectory).filePath(
        QStringLiteral("KY-DIMM_%1").arg(timeStamp));
    if (!QDir().mkpath(runDirectory_)) {
        if (error)
            *error = QStringLiteral("无法创建结果目录：%1").arg(runDirectory_);
        runDirectory_.clear();
        finishRun();
        return false;
    }

    const auto openCsv = [&](QFile &file, const QString &name,
                             const QStringList &header) -> bool {
        file.setFileName(QDir(runDirectory_).filePath(name));
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            if (error)
            *error = QStringLiteral("无法打开输出文件：%1").arg(name);
            finishRun();
            return false;
        }
        QTextStream stream(&file);
        stream << csvLine(header) << '\n';
        return true;
    };

    if (config.storage.saveParameterCsv) {
        if (!openCsv(
                atmosphereFile_, QStringLiteral("atmosphere_summary.csv"),
                {QStringLiteral("timestamp_s"),
                 QStringLiteral("window_end_sequence"),
                 QStringLiteral("valid_sample_count"),
                 QStringLiteral("measured_rate_hz"),
                 QStringLiteral("r0_longitudinal_m"),
                 QStringLiteral("r0_transverse_m"),
                 QStringLiteral("r0_line_of_sight_m"),
                 QStringLiteral("r0_zenith_m"),
                 QStringLiteral("seeing_arcsec"),
                 QStringLiteral("theta0_arcsec"),
                 QStringLiteral("tau0_ms"),
                 QStringLiteral("tau0_valid"),
                 QStringLiteral("tau0_resolution_ms"),
                 QStringLiteral("under_resolved"),
                 QStringLiteral("status")}))
             {
                 finishRun();
                 return false;
             }
    }

    if (config.storage.saveCentroidCsv) {
        if (!openCsv(
                centroidFile_, QStringLiteral("centroid_details.csv"),
                {QStringLiteral("timestamp_s"), QStringLiteral("sequence"),
                 QStringLiteral("star_a_x_px"),
                 QStringLiteral("star_a_y_px"),
                 QStringLiteral("star_b_x_px"),
                 QStringLiteral("star_b_y_px"),
                 QStringLiteral("longitudinal_px"),
                 QStringLiteral("transverse_px"),
                 QStringLiteral("longitudinal_arcsec"),
                 QStringLiteral("transverse_arcsec"),
                 QStringLiteral("roi_a_x"), QStringLiteral("roi_a_y"),
                 QStringLiteral("roi_a_width"),
                 QStringLiteral("roi_a_height"),
                 QStringLiteral("roi_b_x"), QStringLiteral("roi_b_y"),
                 QStringLiteral("roi_b_width"),
                 QStringLiteral("roi_b_height"),
                 QStringLiteral("valid_pair"),
                 QStringLiteral("diagnostic")}))
             {
                 finishRun();
                 return false;
             }
    }

    if (config.storage.saveDiagnosticsCsv) {
        if (!openCsv(
                diagnosticsFile_, QStringLiteral("acquisition_diagnostics.csv"),
                {QStringLiteral("timestamp_s"),
                 QStringLiteral("received_frames"),
                 QStringLiteral("dropped_frames"),
                 QStringLiteral("queue_dropped_frames"),
                 QStringLiteral("measured_rate_hz"),
                 QStringLiteral("average_callback_ms"),
                 QStringLiteral("connected"),
                 QStringLiteral("last_error")}))
             {
                 finishRun();
                 return false;
             }
    }

    if (config.storage.saveRunMetadataJson) {
        QJsonObject metadata;
        QJsonObject physical;
        physical.insert(QStringLiteral("mainApertureMm"),
                        config.optical.mainTelescopeApertureMm);
        physical.insert(QStringLiteral("subApertureMm"),
                        config.optical.subApertureDiameterMm);
        physical.insert(QStringLiteral("baselineMm"),
                        config.optical.baselineSeparationMm);
        physical.insert(QStringLiteral("focalLengthMm"),
                        config.optical.focalLengthMm);
        physical.insert(QStringLiteral("wavelengthNm"),
                        config.optical.wavelengthNm);
        physical.insert(QStringLiteral("pixelSizeUm"),
                        config.optical.pixelSizeUm);
        physical.insert(QStringLiteral("zenithAngleDeg"),
                        config.optical.zenithAngleDeg);
        metadata.insert(QStringLiteral("physical"), physical);

        QJsonObject camera;
        camera.insert(QStringLiteral("modelName"), capabilities.modelName);
        camera.insert(QStringLiteral("serialNumber"), capabilities.serialNumber);
        camera.insert(QStringLiteral("sensorWidth"), capabilities.sensorWidth);
        camera.insert(QStringLiteral("sensorHeight"), capabilities.sensorHeight);
        camera.insert(QStringLiteral("defaultWidth"), capabilities.defaultWidth);
        camera.insert(QStringLiteral("defaultHeight"),
                      capabilities.defaultHeight);
        camera.insert(QStringLiteral("pixelSizeUm"), capabilities.pixelSizeUm);
        camera.insert(QStringLiteral("nominalFrameRateHz"),
                      capabilities.nominalFrameRateHz);
        camera.insert(QStringLiteral("supportsMono8"),
                      capabilities.supportsMono8);
        camera.insert(QStringLiteral("supportsSoftwareTrigger"),
                      capabilities.supportsSoftwareTrigger);
        camera.insert(QStringLiteral("supportsHardwareTrigger"),
                      capabilities.supportsHardwareTrigger);
        camera.insert(QStringLiteral("supportsHardwareAoi"),
                      capabilities.supportsHardwareAoi);
        metadata.insert(QStringLiteral("camera"), camera);

        QJsonObject acquisition;
        acquisition.insert(QStringLiteral("frameWidth"),
                           config.acquisition.frameWidth);
        acquisition.insert(QStringLiteral("frameHeight"),
                           config.acquisition.frameHeight);
        acquisition.insert(QStringLiteral("pixelFormat"),
                           pixelFormatName(config.acquisition.pixelFormat));
        acquisition.insert(QStringLiteral("measurementRateHz"),
                           config.acquisition.measurementRateHz);
        acquisition.insert(QStringLiteral("fullFramePreviewRateHz"),
                           config.acquisition.fullFramePreviewRateHz);
        acquisition.insert(QStringLiteral("roiPreviewRateHz"),
                           config.acquisition.roiPreviewRateHz);
        acquisition.insert(QStringLiteral("exposureTimeMs"),
                           config.acquisition.exposureTimeMs);
        acquisition.insert(QStringLiteral("targetSampleCount"),
                           config.acquisition.targetSampleCount);
        acquisition.insert(QStringLiteral("targetDurationSec"),
                           config.acquisition.targetDurationSec);
        acquisition.insert(QStringLiteral("enableHardwareAoi"),
                           config.acquisition.enableHardwareAoi);
        metadata.insert(QStringLiteral("acquisition"), acquisition);

        QJsonObject processing;
        processing.insert(QStringLiteral("roiWidthPx"),
                          config.processing.roiWidthPx);
        processing.insert(QStringLiteral("roiHeightPx"),
                          config.processing.roiHeightPx);
        processing.insert(QStringLiteral("hardwareAoiMarginPx"),
                          config.processing.hardwareAoiMarginPx);
        processing.insert(QStringLiteral("otsuHistogramBins"),
                          config.processing.otsuHistogramBins);
        processing.insert(QStringLiteral("otsuSigmaThreshold"),
                          config.processing.otsuSigmaThreshold);
        processing.insert(QStringLiteral("otsuPeakFraction"),
                          config.processing.otsuPeakFraction);
        processing.insert(QStringLiteral("connectivity"),
                          config.processing.connectivity);
        processing.insert(QStringLiteral("otsuMinimumComponentAreaPx"),
                          config.processing.otsuMinimumComponentAreaPx);
        processing.insert(QStringLiteral("otsuMaximumComponentAreaPx"),
                          config.processing.otsuMaximumComponentAreaPx);
        processing.insert(QStringLiteral("smallKernelRadiusPx"),
                          config.processing.smallKernelRadiusPx);
        processing.insert(QStringLiteral("minimumPeakDistancePx"),
                          config.processing.minimumPeakDistancePx);
        processing.insert(QStringLiteral("minimumCentroidIntensity"),
                          config.processing.minimumCentroidIntensity);
        processing.insert(QStringLiteral("roiRecenteringDistanceToEdgePx"),
                          config.processing.roiRecenteringDistanceToEdgePx);
        processing.insert(QStringLiteral("roiRecenteringConsecutiveFrames"),
                          config.processing.roiRecenteringConsecutiveFrames);
        processing.insert(QStringLiteral("roiRecenteringCooldownMs"),
                          config.processing.roiRecenteringCooldownMs);
        processing.insert(QStringLiteral("roiRecenteringMinimumShiftPx"),
                          config.processing.roiRecenteringMinimumShiftPx);
        processing.insert(QStringLiteral("roiLostRelocalizationFrames"),
                          config.processing.roiLostRelocalizationFrames);
        processing.insert(QStringLiteral("r0WindowFrames"),
                          config.processing.r0WindowFrames);
        processing.insert(QStringLiteral("resultUpdateIntervalSec"),
                          config.processing.resultUpdateIntervalSec);
        processing.insert(QStringLiteral("tau0HistorySeconds"),
                          config.processing.tau0HistorySeconds);
        processing.insert(QStringLiteral("tau0MaximumLagMs"),
                          config.processing.tau0MaximumLagMs);
        processing.insert(QStringLiteral("tau0MinimumSamples"),
                          config.processing.tau0MinimumSamples);
        metadata.insert(QStringLiteral("processing"), processing);

        QJsonObject trigger;
        trigger.insert(QStringLiteral("mode"),
                       triggerModeName(config.trigger.mode));
        trigger.insert(QStringLiteral("hardwareTriggerLine"),
                       config.trigger.hardwareTriggerLine);
        trigger.insert(QStringLiteral("triggerSelectorFrameStart"),
                       config.trigger.triggerSelectorFrameStart);
        trigger.insert(QStringLiteral("softwareTriggerEachFrame"),
                       config.trigger.softwareTriggerEachFrame);
        trigger.insert(QStringLiteral("allowPartialScan"),
                       config.trigger.allowPartialScan);
        metadata.insert(QStringLiteral("trigger"), trigger);

        QJsonObject storage;
        storage.insert(QStringLiteral("outputDirectory"),
                       config.storage.outputDirectory);
        storage.insert(QStringLiteral("resultRecordIntervalSec"),
                       config.storage.resultRecordIntervalSec);
        storage.insert(QStringLiteral("saveParameterCsv"),
                       config.storage.saveParameterCsv);
        storage.insert(QStringLiteral("saveCentroidCsv"),
                       config.storage.saveCentroidCsv);
        storage.insert(QStringLiteral("saveDiagnosticsCsv"),
                       config.storage.saveDiagnosticsCsv);
        storage.insert(QStringLiteral("saveRunMetadataJson"),
                       config.storage.saveRunMetadataJson);
        storage.insert(QStringLiteral("imageSavingFixedOff"),
                       config.storage.imageSavingFixedOff);
        metadata.insert(QStringLiteral("storage"), storage);

        QSaveFile metadataFile(
            QDir(runDirectory_).filePath(QStringLiteral("run_metadata.json")));
        if (!metadataFile.open(QIODevice::WriteOnly)) {
            if (error)
                *error = QStringLiteral("无法写入 run_metadata.json");
            finishRun();
            return false;
        }
        metadataFile.write(QJsonDocument(metadata).toJson(
            QJsonDocument::Indented));
        if (!metadataFile.commit()) {
            if (error)
                *error = QStringLiteral("run_metadata.json 写入失败");
            finishRun();
            return false;
        }
    }

    running_ = true;
    return true;
}

void ResultWriter::append(const MeasurementResult &result)
{
    if (!running_)
        return;

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (lastResultRecordMs_ >= 0 &&
        nowMs - lastResultRecordMs_ <
            qRound64(resultRecordIntervalSec_ * 1000.0))
        return;
    lastResultRecordMs_ = nowMs;

    if (centroidFile_.isOpen()) {
        QTextStream stream(&centroidFile_);
        const auto &s = result.stars;
        const auto &d = result.differential;
        stream << csvLine({QString::number(nowTimestampSec(), 'f', 6),
                           QString::number(result.sequence),
                           QString::number(s.fullFrameStarA.x(), 'f', 3),
                           QString::number(s.fullFrameStarA.y(), 'f', 3),
                           QString::number(s.fullFrameStarB.x(), 'f', 3),
                           QString::number(s.fullFrameStarB.y(), 'f', 3),
                           QString::number(d.longitudinalPx, 'f', 4),
                           QString::number(d.transversePx, 'f', 4),
                           QString::number(d.longitudinalArcsec, 'f', 6),
                           QString::number(d.transverseArcsec, 'f', 6),
                           QString::number(s.roiA.x),
                           QString::number(s.roiA.y),
                           QString::number(s.roiA.width),
                           QString::number(s.roiA.height),
                           QString::number(s.roiB.x),
                           QString::number(s.roiB.y),
                           QString::number(s.roiB.width),
                           QString::number(s.roiB.height),
                           s.validPair ? QStringLiteral("1")
                                       : QStringLiteral("0"),
                           s.diagnostic})
              << '\n';
        centroidFile_.flush();
    }

    if (atmosphereFile_.isOpen()) {
        QTextStream stream(&atmosphereFile_);
        const auto &a = result.atmosphere;
        stream << csvLine({QString::number(nowTimestampSec(), 'f', 6),
                           QString::number(a.windowEndSequence),
                           QString::number(a.validSampleCount),
                           QString::number(a.measuredRateHz, 'f', 3),
                           QString::number(a.r0LongitudinalM, 'f', 6),
                           QString::number(a.r0TransverseM, 'f', 6),
                           QString::number(a.r0LineOfSightM, 'f', 6),
                           QString::number(a.r0ZenithM, 'f', 6),
                           QString::number(a.seeingArcsec, 'f', 4),
                           QString::number(a.theta0Arcsec, 'f', 4),
                           QString::number(a.tau0Ms, 'f', 4),
                           a.tau0Valid ? QStringLiteral("1")
                                       : QStringLiteral("0"),
                           QString::number(a.tau0ResolutionMs, 'f', 4),
                           a.underResolved ? QStringLiteral("1")
                                           : QStringLiteral("0"),
                           a.statusMessage})
              << '\n';
        atmosphereFile_.flush();
    }
}

void ResultWriter::appendCameraStats(const CameraStatistics &stats)
{
    if (!running_)
        return;
    if (!diagnosticsFile_.isOpen())
        return;

    QTextStream stream(&diagnosticsFile_);
    stream << csvLine({QString::number(nowTimestampSec(), 'f', 6),
                       QString::number(stats.receivedFrames),
                       QString::number(stats.droppedFrames),
                       QString::number(stats.queueDroppedFrames),
                       QString::number(stats.measuredRateHz, 'f', 3),
                       QString::number(stats.averageCallbackMs, 'f', 3),
                       stats.connected ? QStringLiteral("1")
                                       : QStringLiteral("0"),
                       stats.lastError})
          << '\n';
    diagnosticsFile_.flush();
}

void ResultWriter::finishRun()
{
    if (atmosphereFile_.isOpen())
        atmosphereFile_.close();
    if (centroidFile_.isOpen())
        centroidFile_.close();
    if (diagnosticsFile_.isOpen())
        diagnosticsFile_.close();
    running_ = false;
    lastResultRecordMs_ = -1;
    runDirectory_.clear();
}

bool ResultWriter::isRunning() const
{
    return running_;
}

QString ResultWriter::runDirectory() const
{
    return runDirectory_;
}
