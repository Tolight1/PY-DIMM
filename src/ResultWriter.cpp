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

struct LogTimestamp {
    QString epochSeconds;
    QString iso;
};

LogTimestamp nowTimestamp()
{
    const QDateTime current = QDateTime::currentDateTime();
    return {QString::number(current.toMSecsSinceEpoch() / 1000.0, 'f', 6),
            current.toString(Qt::ISODateWithMs)};
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
                 QStringLiteral("timestamp_iso"),
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
                 QStringLiteral("status"),
                 QStringLiteral("configuration_generation"),
                 QStringLiteral("aoi_epoch"),
                 QStringLiteral("aoi_transition_id")}))
             {
                 finishRun();
                 return false;
             }
    }

    if (config.storage.saveCentroidCsv) {
        if (!openCsv(
                centroidFile_, QStringLiteral("centroid_details.csv"),
                {QStringLiteral("timestamp_s"),
                 QStringLiteral("timestamp_iso"),
                 QStringLiteral("sequence"),
                 QStringLiteral("star_a_x_px"),
                 QStringLiteral("star_a_y_px"),
                 QStringLiteral("star_b_x_px"),
                 QStringLiteral("star_b_y_px"),
                 QStringLiteral("longitudinal_px"),
                 QStringLiteral("transverse_px"),
                 QStringLiteral("longitudinal_arcsec"),
                 QStringLiteral("transverse_arcsec"),
                 QStringLiteral("valid_pair"),
                 QStringLiteral("diagnostic"),
                 QStringLiteral("configuration_generation"),
                 QStringLiteral("aoi_epoch"),
                 QStringLiteral("aoi_transition_id"),
                 QStringLiteral("source_rect_x"),
                 QStringLiteral("source_rect_y"),
                 QStringLiteral("source_rect_width"),
                 QStringLiteral("source_rect_height")}))
             {
                 finishRun();
                 return false;
             }
    }

    if (config.storage.saveDiagnosticsCsv) {
        if (!openCsv(
                diagnosticsFile_, QStringLiteral("acquisition_diagnostics.csv"),
                {QStringLiteral("timestamp_s"),
                 QStringLiteral("timestamp_iso"),
                 QStringLiteral("received_frames"),
                 QStringLiteral("dropped_frames"),
                 QStringLiteral("queue_dropped_frames"),
                 QStringLiteral("target_rate_hz"),
                 QStringLiteral("resulting_frame_rate_hz"),
                 QStringLiteral("acquisition_rate_hz"),
                 QStringLiteral("average_callback_ms"),
                 QStringLiteral("connected"),
                 QStringLiteral("rate_diagnostic"),
                QStringLiteral("last_error")}))
             {
                 finishRun();
                 return false;
             }

        if (!openCsv(
                aoiEventsFile_, QStringLiteral("aoi_events.csv"),
                {QStringLiteral("timestamp_s"),
                 QStringLiteral("timestamp_iso"),
                 QStringLiteral("event"),
                 QStringLiteral("aoi_epoch"),
                 QStringLiteral("aoi_transition_id"),
                 QStringLiteral("frame_sequence"),
                 QStringLiteral("frame_timestamp_s"),
                 QStringLiteral("frame_generation"),
                 QStringLiteral("previous_generation"),
                 QStringLiteral("requested_generation"),
                 QStringLiteral("applied_generation"),
                 QStringLiteral("stale_frames_dropped"),
                 QStringLiteral("requested_x"),
                 QStringLiteral("requested_y"),
                 QStringLiteral("requested_width"),
                 QStringLiteral("requested_height"),
                 QStringLiteral("applied_x"),
                 QStringLiteral("applied_y"),
                 QStringLiteral("applied_width"),
                 QStringLiteral("applied_height"),
                 QStringLiteral("star_a_x_px"),
                 QStringLiteral("star_a_y_px"),
                 QStringLiteral("star_b_x_px"),
                 QStringLiteral("star_b_y_px"),
                 QStringLiteral("longitudinal_px"),
                 QStringLiteral("transverse_px"),
                 QStringLiteral("differential_jump_px"),
                 QStringLiteral("differential_baseline_deviation_px"),
                 QStringLiteral("differential_baseline_violation_frames"),
                 QStringLiteral("valid_pair"),
                 QStringLiteral("sample_accepted"),
                 QStringLiteral("valid_sample_count"),
                 QStringLiteral("reason")}))
              {
                  finishRun();
                  return false;
              }

        if (!openCsv(
                aoiTransitionSamplesFile_,
                QStringLiteral("aoi_transition_samples.csv"),
                {QStringLiteral("timestamp_s"),
                 QStringLiteral("timestamp_iso"),
                 QStringLiteral("aoi_epoch"),
                 QStringLiteral("aoi_transition_id"),
                 QStringLiteral("frame_index_after_apply"),
                 QStringLiteral("frame_sequence"),
                 QStringLiteral("frame_timestamp_s"),
                 QStringLiteral("frame_generation"),
                 QStringLiteral("source_rect_x"),
                 QStringLiteral("source_rect_y"),
                 QStringLiteral("source_rect_width"),
                 QStringLiteral("source_rect_height"),
                 QStringLiteral("applied_aoi_x"),
                 QStringLiteral("applied_aoi_y"),
                 QStringLiteral("applied_aoi_width"),
                 QStringLiteral("applied_aoi_height"),
                 QStringLiteral("star_a_x_px"),
                 QStringLiteral("star_a_y_px"),
                 QStringLiteral("star_a_peak_intensity"),
                 QStringLiteral("star_a_component_area_px"),
                 QStringLiteral("star_b_x_px"),
                 QStringLiteral("star_b_y_px"),
                 QStringLiteral("star_b_peak_intensity"),
                 QStringLiteral("star_b_component_area_px"),
                 QStringLiteral("longitudinal_px"),
                 QStringLiteral("transverse_px"),
                 QStringLiteral("differential_jump_px"),
                 QStringLiteral("differential_baseline_deviation_px"),
                 QStringLiteral("differential_baseline_violation_frames"),
                 QStringLiteral("star_a_valid"),
                 QStringLiteral("star_b_valid"),
                 QStringLiteral("valid_pair"),
                 QStringLiteral("sample_accepted"),
                 QStringLiteral("differential_continuity_rejected"),
                 QStringLiteral("request_full_frame_relocalization"),
                 QStringLiteral("diagnostic")}))
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
        acquisition.insert(QStringLiteral("previewRateHz"),
                           config.acquisition.previewRateHz);
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
        processing.insert(QStringLiteral("centroidKernelRadiusPx"),
                          config.processing.centroidKernelRadiusPx);
        processing.insert(QStringLiteral("hardwareAoiMarginPx"),
                          config.processing.hardwareAoiMarginPx);
        processing.insert(QStringLiteral("hardwareAoiMaxWidthPx"),
                          config.processing.hardwareAoiMaxWidthPx);
        processing.insert(QStringLiteral("hardwareAoiMaxHeightPx"),
                          config.processing.hardwareAoiMaxHeightPx);
        processing.insert(QStringLiteral("hardwareAoiUpdateDistanceToEdgePx"),
                          config.processing.hardwareAoiUpdateDistanceToEdgePx);
        processing.insert(QStringLiteral("hardwareAoiUpdateMinimumShiftPx"),
                          config.processing.hardwareAoiUpdateMinimumShiftPx);
        processing.insert(QStringLiteral("hardwareAoiUpdateCooldownMs"),
                          config.processing.hardwareAoiUpdateCooldownMs);
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
        processing.insert(QStringLiteral("minimumPeakDistancePx"),
                          config.processing.minimumPeakDistancePx);
        processing.insert(QStringLiteral("minimumCentroidIntensity"),
                          config.processing.minimumCentroidIntensity);
        processing.insert(QStringLiteral("maximumDifferentialJumpPx"),
                          config.processing.maximumDifferentialJumpPx);
        processing.insert(QStringLiteral("differentialBaselineWindowFrames"),
                          kDifferentialBaselineWindowFrames);
        processing.insert(QStringLiteral("differentialBaselineViolationFrames"),
                          config.processing.differentialBaselineViolationFrames);
        processing.insert(QStringLiteral("aoiTransitionDiagnosticFrameCount"),
                          kAoiTransitionDiagnosticFrameCount);
        processing.insert(QStringLiteral("lostPairRelocalizationFrames"),
                          config.processing.lostPairRelocalizationFrames);
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
        const LogTimestamp timestamp = nowTimestamp();
        stream << csvLine({timestamp.epochSeconds,
                           timestamp.iso,
                           QString::number(result.sequence),
                           QString::number(s.fullFrameStarA.x(), 'f', 3),
                           QString::number(s.fullFrameStarA.y(), 'f', 3),
                           QString::number(s.fullFrameStarB.x(), 'f', 3),
                           QString::number(s.fullFrameStarB.y(), 'f', 3),
                           QString::number(d.longitudinalPx, 'f', 4),
                           QString::number(d.transversePx, 'f', 4),
                           QString::number(d.longitudinalArcsec, 'f', 6),
                           QString::number(d.transverseArcsec, 'f', 6),
                           s.validPair ? QStringLiteral("1")
                                       : QStringLiteral("0"),
                           s.diagnostic,
                           QString::number(result.configurationGeneration),
                           QString::number(result.aoiEpoch),
                           QString::number(result.transitionId),
                           QString::number(result.sourceRect.x()),
                           QString::number(result.sourceRect.y()),
                           QString::number(result.sourceRect.width()),
                           QString::number(result.sourceRect.height())})
              << '\n';
        centroidFile_.flush();
    }

    if (atmosphereFile_.isOpen()) {
        QTextStream stream(&atmosphereFile_);
        const auto &a = result.atmosphere;
        const LogTimestamp timestamp = nowTimestamp();
        stream << csvLine({timestamp.epochSeconds,
                           timestamp.iso,
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
                           a.statusMessage,
                           QString::number(result.configurationGeneration),
                           QString::number(result.aoiEpoch),
                           QString::number(result.transitionId)})
              << '\n';
        atmosphereFile_.flush();
    }
}

void ResultWriter::appendAoiEvent(const AoiEvent &event)
{
    if (!running_ || !aoiEventsFile_.isOpen())
        return;

    const auto rectFields = [](const RoiRect &rect) {
        return QStringList{QString::number(rect.x),
                           QString::number(rect.y),
                           QString::number(rect.width),
                           QString::number(rect.height)};
    };

    const LogTimestamp timestamp = nowTimestamp();
    QStringList fields{timestamp.epochSeconds,
                       timestamp.iso,
                       event.type,
                       QString::number(event.aoiEpoch),
                       QString::number(event.transitionId),
                       QString::number(event.frameSequence),
                       QString::number(event.frameTimestampSec, 'f', 9),
                       QString::number(event.frameGeneration),
                       QString::number(event.previousGeneration),
                       QString::number(event.requestedGeneration),
                       QString::number(event.appliedGeneration),
                       QString::number(event.staleFramesDropped)};
    fields.append(rectFields(event.requestedAoi));
    fields.append(rectFields(event.appliedAoi));
    fields.append({QString::number(event.starA.x(), 'f', 3),
                   QString::number(event.starA.y(), 'f', 3),
                   QString::number(event.starB.x(), 'f', 3),
                   QString::number(event.starB.y(), 'f', 3),
                   QString::number(event.longitudinalPx, 'f', 4),
                   QString::number(event.transversePx, 'f', 4),
                   QString::number(event.differentialJumpPx, 'f', 4),
                   QString::number(event.differentialBaselineDeviationPx, 'f', 4),
                   QString::number(event.differentialBaselineViolationFrames),
                   event.validPair ? QStringLiteral("1")
                                   : QStringLiteral("0"),
                   event.sampleAccepted ? QStringLiteral("1")
                                        : QStringLiteral("0"),
                   QString::number(event.validSampleCount),
                   event.reason});

    QTextStream stream(&aoiEventsFile_);
    stream << csvLine(fields) << '\n';
    aoiEventsFile_.flush();
}

void ResultWriter::appendAoiTransitionSample(
    const AoiTransitionSample &sample)
{
    if (!running_ || !aoiTransitionSamplesFile_.isOpen())
        return;

    const LogTimestamp timestamp = nowTimestamp();
    QTextStream stream(&aoiTransitionSamplesFile_);
    stream << csvLine({timestamp.epochSeconds,
                       timestamp.iso,
                       QString::number(sample.aoiEpoch),
                       QString::number(sample.transitionId),
                       QString::number(sample.frameIndexAfterApply),
                       QString::number(sample.frameSequence),
                       QString::number(sample.frameTimestampSec, 'f', 9),
                       QString::number(sample.frameGeneration),
                       QString::number(sample.sourceRect.x()),
                       QString::number(sample.sourceRect.y()),
                       QString::number(sample.sourceRect.width()),
                       QString::number(sample.sourceRect.height()),
                       QString::number(sample.appliedAoi.x),
                       QString::number(sample.appliedAoi.y),
                       QString::number(sample.appliedAoi.width),
                       QString::number(sample.appliedAoi.height),
                       QString::number(sample.starA.x(), 'f', 3),
                       QString::number(sample.starA.y(), 'f', 3),
                       QString::number(sample.starAPeakIntensity, 'f', 3),
                       QString::number(sample.starAComponentAreaPx),
                       QString::number(sample.starB.x(), 'f', 3),
                       QString::number(sample.starB.y(), 'f', 3),
                       QString::number(sample.starBPeakIntensity, 'f', 3),
                       QString::number(sample.starBComponentAreaPx),
                       QString::number(sample.longitudinalPx, 'f', 4),
                       QString::number(sample.transversePx, 'f', 4),
                       QString::number(sample.differentialJumpPx, 'f', 4),
                       QString::number(sample.differentialBaselineDeviationPx, 'f', 4),
                       QString::number(sample.differentialBaselineViolationFrames),
                       sample.starAValid ? QStringLiteral("1")
                                         : QStringLiteral("0"),
                       sample.starBValid ? QStringLiteral("1")
                                         : QStringLiteral("0"),
                       sample.validPair ? QStringLiteral("1")
                                        : QStringLiteral("0"),
                       sample.sampleAccepted ? QStringLiteral("1")
                                              : QStringLiteral("0"),
                       sample.differentialContinuityRejected
                           ? QStringLiteral("1")
                           : QStringLiteral("0"),
                       sample.requestFullFrameRelocalization
                           ? QStringLiteral("1")
                           : QStringLiteral("0"),
                       sample.diagnostic})
           << '\n';
    aoiTransitionSamplesFile_.flush();
}

void ResultWriter::appendCameraStats(const CameraStatistics &stats)
{
    if (!running_)
        return;
    if (!diagnosticsFile_.isOpen())
        return;

    const LogTimestamp timestamp = nowTimestamp();
    QTextStream stream(&diagnosticsFile_);
    stream << csvLine({timestamp.epochSeconds,
                       timestamp.iso,
                       QString::number(stats.receivedFrames),
                       QString::number(stats.droppedFrames),
                       QString::number(stats.queueDroppedFrames),
                       QString::number(stats.targetRateHz, 'f', 3),
                       QString::number(stats.resultingFrameRateHz, 'f', 3),
                       QString::number(stats.acquisitionRateHz, 'f', 3),
                       QString::number(stats.averageCallbackMs, 'f', 3),
                       stats.connected ? QStringLiteral("1")
                                       : QStringLiteral("0"),
                       stats.rateDiagnostic,
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
    if (aoiEventsFile_.isOpen())
        aoiEventsFile_.close();
    if (aoiTransitionSamplesFile_.isOpen())
        aoiTransitionSamplesFile_.close();
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
