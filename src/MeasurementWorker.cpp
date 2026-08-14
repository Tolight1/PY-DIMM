#include "MeasurementWorker.h"

#include <QDateTime>
#include <QTimer>

#include <opencv2/core.hpp>

#include <algorithm>
#include <cmath>

MeasurementWorker::MeasurementWorker(FrameQueue &queue,
                                     DisplayMailbox &displayMailbox,
                                     QObject *parent)
    : QObject(parent)
    , queue_(queue)
    , displayMailbox_(displayMailbox)
    , tracker_(AppConfig::defaults())
    , calculator_(AppConfig::defaults())
{
}

void MeasurementWorker::configure(AppConfig config)
{
    config_ = config;
    tracker_ = TwoStarTracker(config);
    calculator_ = AtmosphereCalculator(config);
}

void MeasurementWorker::start()
{
    resetState();
    if (!pollTimer_) {
        pollTimer_ = new QTimer(this);
        pollTimer_->setInterval(2);
    }
    connect(pollTimer_, &QTimer::timeout, this, &MeasurementWorker::poll,
            Qt::UniqueConnection);
    running_ = true;
    pollTimer_->start();
}

void MeasurementWorker::stop()
{
    running_ = false;
    if (pollTimer_)
        pollTimer_->stop();
    emit stopped();
}

void MeasurementWorker::resetState()
{
    initialLocateDone_ = false;
    aoiChangePending_ = false;
    expectedAoiGeneration_ = 0;
    validPairCount_ = 0;
    validPairsSinceLastResult_ = 0;
    resultEmitted_ = false;
    lastHeartbeatMs_ = 0;
    lastPreviewMs_ = 0;
    rateTimestampsNs_.clear();
    lastResult_ = MeasurementResult{};
    tracker_.reset();
    calculator_.reset();
}

void MeasurementWorker::onHardwareAoiApplied(RoiRect aoi, std::uint64_t generation)
{
    tracker_.applyHardwareAoi(aoi, generation);
    // The camera's generation counter is authoritative; accept frames stamped
    // with this generation once the pending AOI change takes effect.
    expectedAoiGeneration_ = generation;
}

void MeasurementWorker::onHardwareAoiFailed(QString message)
{
    aoiChangePending_ = false;
    expectedAoiGeneration_ = 0;
    initialLocateDone_ = false;
    tracker_.reset();
    if (config_.acquisition.enableHardwareAoi)
        emit fullFrameRequested();
    emit measurementError(QStringLiteral("硬件 AOI 配置失败：%1").arg(message));
}

void MeasurementWorker::poll()
{
    if (!running_)
        return;
    CameraFrame frame;
    if (queue_.tryPopLatest(frame))
        processOneFrame(frame);
}

void MeasurementWorker::processOneFrame(const CameraFrame &frame)
{
    if (!running_)
        return;
    if (!frame.isValid()) {
        emit measurementError(QStringLiteral("测量线程收到无效帧"));
        return;
    }

    const QRect fullRect(0, 0, config_.acquisition.frameWidth,
                         config_.acquisition.frameHeight);
    const bool isFullFrame = (frame.sourceRect == fullRect);

    // While an AOI change is pending, only frames carrying the expected camera
    // generation are consumed; older-generation frames are dropped so stale
    // coordinates never contaminate the measurement window.
    if (aoiChangePending_) {
        if (frame.configurationGeneration != expectedAoiGeneration_)
            return;
        aoiChangePending_ = false;
    }

    const double timestampSec = frame.timestampNs * 1.0e-9;

    if (isFullFrame && !initialLocateDone_) {
        // Full-frame locating phase: identify the two spots once. Hardware AOI
        // is optional; with it disabled, the same full-frame image continues
        // through the software-ROI path below.
        const TrackerUpdate locateUpdate = tracker_.locateFromFullFrame(frame);
        if (!locateUpdate.requestHardwareAoi) {
            maybeEmitStatus(frame.sequence, frame.timestampNs, timestampSec,
                            locateUpdate.measurement, DifferentialSample{});
            emit roiStateChanged(locateUpdate.overlay);
            return;
        }

        initialLocateDone_ = true;
        if (config_.acquisition.enableHardwareAoi) {
            emit roiStateChanged(locateUpdate.overlay);
            aoiChangePending_ = true;
            expectedAoiGeneration_ = locateUpdate.requestGeneration;
            emit hardwareAoiRequested(locateUpdate.requestedHardwareAoi,
                                      locateUpdate.requestGeneration);
            maybeEmitStatus(frame.sequence, frame.timestampNs, timestampSec,
                            locateUpdate.measurement, DifferentialSample{});
            return;
        }

        // Software-only mode keeps the camera at 1920 × 1200. Tell the
        // tracker that its current AOI is the full sensor so ROI coordinates
        // can still be converted and cropped consistently.
        tracker_.applyHardwareAoi(
            RoiRect{0, 0, config_.acquisition.frameWidth,
                    config_.acquisition.frameHeight},
            0);
    }

    // Hardware-AOI or software-only phase: crop the two software ROIs and
    // measure. In hardware-AOI mode, a full-frame image is only expected
    // during locating and must not be processed as an AOI frame.
    if (isFullFrame && config_.acquisition.enableHardwareAoi) {
        maybeEmitStatus(frame.sequence, frame.timestampNs, timestampSec,
                        TwoStarMeasurement{}, DifferentialSample{});
        return;
    }
    const TrackerUpdate update = tracker_.processAoiFrame(frame);

    if (update.requestFullFrameRelocalization) {
        // Pair lost: return to full-frame locating. Reset the tracker so its
        // generation counter restarts from zero, matching the camera after
        // resetToFullFrame.
        tracker_.reset();
        initialLocateDone_ = false;
        aoiChangePending_ = false;
        expectedAoiGeneration_ = 0;
        if (config_.acquisition.enableHardwareAoi)
            emit fullFrameRequested();
        maybeEmitStatus(frame.sequence, frame.timestampNs, timestampSec,
                        TwoStarMeasurement{},
                       DifferentialSample{});
        return;
    }

    if (update.measurement.validPair) {
        DifferentialSample sample = AtmosphereCalculator::makeSample(
            update.measurement, config_.optical, timestampSec);
        if (update.requestHardwareAoi) {
            if (config_.acquisition.enableHardwareAoi) {
                // Recentering request: ask for a new AOI and stop appending
                // samples until the new generation's frames arrive.
                aoiChangePending_ = true;
                expectedAoiGeneration_ = update.requestGeneration;
                emit hardwareAoiRequested(update.requestedHardwareAoi,
                                          update.requestGeneration);
                maybeEmitStatus(frame.sequence, frame.timestampNs, timestampSec,
                                update.measurement, DifferentialSample{});
            } else if (sample.valid) {
                // Software-only mode can immediately use the newly moved
                // software ROIs in the same full-frame stream.
                sample.sequence = frame.sequence;
                calculator_.append(sample);
                ++validPairCount_;
                ++validPairsSinceLastResult_;
                rateTimestampsNs_.push_back(frame.timestampNs);
                while (rateTimestampsNs_.size() > 100)
                    rateTimestampsNs_.pop_front();
                maybeEmitStatus(frame.sequence, frame.timestampNs, timestampSec,
                                update.measurement, sample);
            }
        } else if (sample.valid) {
            // One differential sample per valid paired frame.
            sample.sequence = frame.sequence;
            calculator_.append(sample);
            ++validPairCount_;
            ++validPairsSinceLastResult_;
            rateTimestampsNs_.push_back(frame.timestampNs);
            while (rateTimestampsNs_.size() > 100)
                rateTimestampsNs_.pop_front();
            maybeEmitStatus(frame.sequence, frame.timestampNs, timestampSec,
                           update.measurement,
                           sample);
        }
    } else {
        maybeEmitStatus(frame.sequence, frame.timestampNs, timestampSec,
                       update.measurement,
                       DifferentialSample{});
    }

    publishDisplay(frame, update, lastResult_);
}

void MeasurementWorker::publishDisplay(const CameraFrame &frame,
                                       const TrackerUpdate &update,
                                       const MeasurementResult &result)
{
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const int intervalMs = qMax(
        1, qRound(1000.0 / config_.acquisition.roiPreviewRateHz));
    if (nowMs - lastPreviewMs_ < intervalMs)
        return;
    lastPreviewMs_ = nowMs;

    DisplaySnapshot snapshot;
    snapshot.sequence = frame.sequence;
    snapshot.overlay = update.overlay;
    snapshot.latestResult = result;

    // Crop each software ROI from the AOI frame with sourceRect-aware
    // coordinates. processAoiFrame already validated containment.
    const QPoint topLeft = frame.sourceRect.topLeft();
    const RoiRect roiA = tracker_.roiA();
    const RoiRect roiB = tracker_.roiB();
    const cv::Rect localA(roiA.x - topLeft.x(), roiA.y - topLeft.y(),
                          roiA.width, roiA.height);
    const cv::Rect localB(roiB.x - topLeft.x(), roiB.y - topLeft.y(),
                          roiB.width, roiB.height);
    const cv::Rect imageRect(0, 0, frame.mono8.cols, frame.mono8.rows);
    if ((localA & imageRect) == localA)
        snapshot.roiAMono8 = cv::Mat(frame.mono8, localA).clone();
    if ((localB & imageRect) == localB)
        snapshot.roiBMono8 = cv::Mat(frame.mono8, localB).clone();

    displayMailbox_.publish(snapshot);
    emit roiStateChanged(update.overlay);
}

void MeasurementWorker::maybeEmitStatus(std::uint64_t sequence,
                                        std::uint64_t frameTimestampNs,
                                        double timestampSec,
                                        const TwoStarMeasurement &stars,
                                        const DifferentialSample &sample)
{
    // Wall-clock heartbeat paces status refresh only. Result publication is
    // frame-count driven and must not be delayed by the GUI heartbeat.
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const bool heartbeatDue = nowMs - lastHeartbeatMs_ >= 500;

    // Result update schedule: first result after the r0 window fills, then
    // every round(measurementRateHz * resultUpdateIntervalSec) valid pairs.
    const int r0Window = config_.processing.r0WindowFrames;
    const int intervalFrames = qMax(
        1, qRound(config_.acquisition.measurementRateHz *
                  config_.processing.resultUpdateIntervalSec));
    bool scheduleDue = false;
    if (!resultEmitted_)
        scheduleDue = (static_cast<long long>(validPairCount_) >= r0Window);
    else
        scheduleDue = (static_cast<long long>(validPairsSinceLastResult_) >=
                       intervalFrames);
    if (scheduleDue) {
        validPairsSinceLastResult_ = 0;
        resultEmitted_ = true;
    }

    if (!heartbeatDue && !scheduleDue)
        return;

    if (heartbeatDue)
        lastHeartbeatMs_ = nowMs;

    const double measuredRateHz = currentRateHz(frameTimestampNs);
    const AtmosphereResult atmo =
        calculator_.calculate(sequence, measuredRateHz);

    MeasurementResult result;
    result.sequence = sequence;
    result.timestampSec = timestampSec;
    result.stars = stars;
    result.differential = sample;
    result.atmosphere = atmo;
    if (!stars.validPair && !stars.diagnostic.isEmpty() &&
        result.atmosphere.validSampleCount == 0) {
        result.atmosphere.statusMessage = stars.diagnostic;
    }
    if (resultEmitted_ && !scheduleDue && result.atmosphere.valid) {
        // Keep the published four parameters stable between frame-count
        // update points, while allowing the progress/status fields to follow
        // the current rolling window.
        const AtmosphereResult published = lastResult_.atmosphere;
        result.atmosphere.r0LongitudinalM = published.r0LongitudinalM;
        result.atmosphere.r0TransverseM = published.r0TransverseM;
        result.atmosphere.r0LineOfSightM = published.r0LineOfSightM;
        result.atmosphere.r0ZenithM = published.r0ZenithM;
        result.atmosphere.seeingArcsec = published.seeingArcsec;
        result.atmosphere.theta0Arcsec = published.theta0Arcsec;
        result.atmosphere.tau0Ms = published.tau0Ms;
        result.atmosphere.underResolved = published.underResolved;
        result.atmosphere.valid = published.valid;
        result.atmosphere.tau0Valid = published.tau0Valid;
        result.atmosphere.tau0ResolutionMs = published.tau0ResolutionMs;
    }
    lastResult_ = result;
    if (scheduleDue)
        emit resultReady(result);
    emit measurementStatusReady(result);
    if (heartbeatDue || scheduleDue)
        emit measurementStatsUpdated(measuredRateHz, validPairCount_);
}

double MeasurementWorker::currentRateHz(std::uint64_t referenceTimestampNs) const
{
    if (rateTimestampsNs_.size() < 2 || referenceTimestampNs == 0)
        return 0.0;
    const std::uint64_t newest = rateTimestampsNs_.back();
    if (referenceTimestampNs <= newest) {
        const double dtSec =
            (newest - rateTimestampsNs_.front()) * 1.0e-9;
        if (dtSec <= 0.0)
            return 0.0;
        return (static_cast<double>(rateTimestampsNs_.size()) - 1.0) / dtSec;
    }

    const double idleSec = (referenceTimestampNs - newest) * 1.0e-9;
    if (idleSec >= 1.0)
        return 0.0;
    const double dtSec =
        (newest - rateTimestampsNs_.front()) * 1.0e-9 + idleSec;
    if (dtSec <= 0.0)
        return 0.0;
    return (static_cast<double>(rateTimestampsNs_.size()) - 1.0) / dtSec;
}
