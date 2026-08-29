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
    staleFramesDroppedSinceAoi_ = 0;
    awaitingFirstValidAfterAoi_ = false;
    aoiEpoch_ = 0;
    nextTransitionId_ = 0;
    pendingTransitionId_ = 0;
    pendingPreviousGeneration_ = 0;
    pendingRequestedGeneration_ = 0;
    pendingRequestedAoi_ = RoiRect{0, 0, 0, 0};
    activeTransitionId_ = 0;
    activePreviousGeneration_ = 0;
    activeRequestedGeneration_ = 0;
    activeRequestedAoi_ = RoiRect{0, 0, 0, 0};
    activeAppliedAoi_ = RoiRect{0, 0, 0, 0};
    aoiTransitionDiagnosticFrameIndex_ = 0;
    aoiTransitionDiagnosticFramesRemaining_ = 0;
    validPairCount_ = 0;
    resultEmitted_ = false;
    measurementTemporarilyInvalid_ = false;
    lastResultTimestampSec_ = 0.0;
    lastHeartbeatMs_ = 0;
    lastPreviewMs_ = 0;
    rateTimestampsNs_.clear();
    lastResult_ = MeasurementResult{};
    tracker_.reset();
    calculator_.reset();
}

void MeasurementWorker::onHardwareAoiApplied(RoiRect aoi, std::uint64_t generation)
{
    AoiEvent event;
    event.type = QStringLiteral("applied");
    event.aoiEpoch = aoiEpoch_;
    event.transitionId = pendingTransitionId_;
    event.previousGeneration = pendingPreviousGeneration_;
    event.requestedGeneration = pendingRequestedGeneration_;
    event.appliedGeneration = generation;
    event.requestedAoi = pendingRequestedAoi_;
    event.appliedAoi = aoi;
    event.staleFramesDropped = staleFramesDroppedSinceAoi_;
    event.reason = QStringLiteral("相机已应用读回 AOI");
    emit aoiEvent(event);

    activeTransitionId_ = pendingTransitionId_;
    activePreviousGeneration_ = pendingPreviousGeneration_;
    activeRequestedGeneration_ = pendingRequestedGeneration_;
    activeRequestedAoi_ = pendingRequestedAoi_;
    activeAppliedAoi_ = aoi;
    aoiTransitionDiagnosticFrameIndex_ = 0;
    aoiTransitionDiagnosticFramesRemaining_ =
        kAoiTransitionDiagnosticFrameCount;
    pendingTransitionId_ = 0;
    pendingPreviousGeneration_ = 0;
    pendingRequestedGeneration_ = 0;
    pendingRequestedAoi_ = RoiRect{0, 0, 0, 0};

    tracker_.applyHardwareAoi(aoi, generation);
    // The camera's generation counter is authoritative; accept frames stamped
    // with this generation once the pending AOI change takes effect.
    expectedAoiGeneration_ = generation;
    // Publish the camera readback immediately.  This keeps the UI overlay in
    // the same coordinate system as the frames that will follow it.
    publishOverlayState();
    emit starStateChanged(tracker_.currentOverlay());
}

void MeasurementWorker::onHardwareAoiFailed(QString message)
{
    AoiEvent event;
    event.type = QStringLiteral("failed");
    event.aoiEpoch = aoiEpoch_;
    event.transitionId = pendingTransitionId_;
    event.previousGeneration = pendingPreviousGeneration_;
    event.requestedGeneration = pendingRequestedGeneration_;
    event.requestedAoi = pendingRequestedAoi_;
    event.staleFramesDropped = staleFramesDroppedSinceAoi_;
    event.reason = message;
    emit aoiEvent(event);

    aoiChangePending_ = false;
    expectedAoiGeneration_ = 0;
    staleFramesDroppedSinceAoi_ = 0;
    awaitingFirstValidAfterAoi_ = false;
    aoiTransitionDiagnosticFrameIndex_ = 0;
    aoiTransitionDiagnosticFramesRemaining_ = 0;
    initialLocateDone_ = false;
    tracker_.reset();
    publishOverlayState();
    if (config_.acquisition.enableHardwareAoi) {
        pendingTransitionId_ = ++nextTransitionId_;
        pendingPreviousGeneration_ = 0;
        pendingRequestedGeneration_ = 0;
        pendingRequestedAoi_ = RoiRect{0, 0,
                                      config_.acquisition.frameWidth,
                                      config_.acquisition.frameHeight};
        emit fullFrameRequested();
    }
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
        if (frame.configurationGeneration != expectedAoiGeneration_) {
            ++staleFramesDroppedSinceAoi_;
            return;
        }
        aoiChangePending_ = false;
    }

    const double timestampSec = frame.timestampNs * 1.0e-9;

    if (isFullFrame && !initialLocateDone_) {
        // Full-frame locating phase: identify the two spots once. Hardware AOI
        // is optional; with it disabled, the same full-frame image continues
        // through the centroid path below.
        const TrackerUpdate locateUpdate = tracker_.locateFromFullFrame(frame);
        if (!locateUpdate.requestHardwareAoi) {
            publishOverlayState(frame.sequence);
            maybeEmitStatus(frame.sequence, frame.timestampNs, timestampSec,
                            locateUpdate.measurement, DifferentialSample{},
                            frame.configurationGeneration, frame.sourceRect);
            maybeEmitAoiTransitionSample(frame, locateUpdate,
                                          DifferentialSample{}, false,
                                          timestampSec);
            emit starStateChanged(locateUpdate.overlay);
            return;
        }

        initialLocateDone_ = true;
        if (config_.acquisition.enableHardwareAoi) {
            const std::uint64_t previousGeneration =
                expectedAoiGeneration_;
            pendingTransitionId_ = ++nextTransitionId_;
            pendingPreviousGeneration_ = previousGeneration;
            pendingRequestedGeneration_ = locateUpdate.requestGeneration;
            pendingRequestedAoi_ = locateUpdate.requestedHardwareAoi;

            AoiEvent event;
            event.type = QStringLiteral("request");
            event.aoiEpoch = aoiEpoch_;
            event.transitionId = pendingTransitionId_;
            event.frameSequence = frame.sequence;
            event.frameTimestampSec = timestampSec;
            event.frameGeneration = frame.configurationGeneration;
            event.previousGeneration = previousGeneration;
            event.requestedGeneration = locateUpdate.requestGeneration;
            event.requestedAoi = locateUpdate.requestedHardwareAoi;
            event.starA = locateUpdate.measurement.fullFrameStarA;
            event.starB = locateUpdate.measurement.fullFrameStarB;
            event.validPair = locateUpdate.measurement.validPair;
            event.reason = locateUpdate.measurement.diagnostic;
            emit aoiEvent(event);

            emit starStateChanged(locateUpdate.overlay);
            aoiChangePending_ = true;
            expectedAoiGeneration_ = locateUpdate.requestGeneration;
            staleFramesDroppedSinceAoi_ = 0;
            awaitingFirstValidAfterAoi_ = true;
            emit hardwareAoiRequested(locateUpdate.requestedHardwareAoi,
                                      locateUpdate.requestGeneration);
            maybeEmitStatus(frame.sequence, frame.timestampNs, timestampSec,
                            locateUpdate.measurement, DifferentialSample{},
                            frame.configurationGeneration, frame.sourceRect);
            const DifferentialSample locateSample =
                locateUpdate.measurement.validPair
                    ? AtmosphereCalculator::makeSample(
                          locateUpdate.measurement, config_.optical,
                          timestampSec)
                    : DifferentialSample{};
            maybeEmitAoiTransitionSample(frame, locateUpdate, locateSample,
                                          false, timestampSec);
            return;
        }

        // Software-only mode keeps the camera at 1920 × 1200. Tell the
        // tracker that its current AOI is the full sensor.
        tracker_.applyHardwareAoi(
            RoiRect{0, 0, config_.acquisition.frameWidth,
                    config_.acquisition.frameHeight},
            0);
        activeAppliedAoi_ = RoiRect{0, 0, config_.acquisition.frameWidth,
                                    config_.acquisition.frameHeight};
    }

    // Hardware-AOI or software-only phase: discover two candidates in the
    // current image, then refine each around its centroid. In hardware-AOI
    // mode, a full-frame image is only expected
    // during locating and must not be processed as an AOI frame.
    if (isFullFrame && config_.acquisition.enableHardwareAoi) {
        const TrackerUpdate noUpdate;
        maybeEmitAoiTransitionSample(frame, noUpdate, DifferentialSample{},
                                     false, timestampSec);
        maybeEmitStatus(frame.sequence, frame.timestampNs, timestampSec,
                        TwoStarMeasurement{}, DifferentialSample{},
                        frame.configurationGeneration, frame.sourceRect);
        return;
    }
    const TrackerUpdate update = tracker_.processAoiFrame(frame);

    if (update.differentialContinuityRejected) {
        AoiEvent event;
        event.type = QStringLiteral("sample_rejected");
        event.aoiEpoch = aoiEpoch_;
        event.transitionId = activeTransitionId_;
        event.frameSequence = frame.sequence;
        event.frameTimestampSec = timestampSec;
        event.frameGeneration = frame.configurationGeneration;
        event.previousGeneration = activePreviousGeneration_;
        event.requestedGeneration = activeRequestedGeneration_;
        event.appliedGeneration = frame.configurationGeneration;
        event.requestedAoi = activeRequestedAoi_;
        event.appliedAoi = activeAppliedAoi_;
        event.starA = update.measurement.fullFrameStarA;
        event.starB = update.measurement.fullFrameStarB;
        event.differentialJumpPx = update.differentialJumpPx;
        event.differentialBaselineDeviationPx =
            update.differentialBaselineDeviationPx;
        event.differentialBaselineViolationFrames =
            update.differentialBaselineViolationFrames;
        event.validPair = false;
        event.sampleAccepted = false;
        event.validSampleCount = validPairCount_;
        event.reason = update.measurement.diagnostic;
        emit aoiEvent(event);
    }

    if (update.requestFullFrameRelocalization) {
        maybeEmitAoiTransitionSample(frame, update, DifferentialSample{},
                                     false, timestampSec);
        ++aoiEpoch_;
        pendingTransitionId_ = ++nextTransitionId_;
        pendingPreviousGeneration_ = expectedAoiGeneration_;
        pendingRequestedGeneration_ = 0;
        pendingRequestedAoi_ = RoiRect{0, 0,
                                      config_.acquisition.frameWidth,
                                      config_.acquisition.frameHeight};

        AoiEvent event;
        event.type = QStringLiteral("relocalize");
        event.aoiEpoch = aoiEpoch_;
        event.transitionId = pendingTransitionId_;
        event.frameSequence = frame.sequence;
        event.frameTimestampSec = timestampSec;
        event.frameGeneration = frame.configurationGeneration;
        event.previousGeneration = expectedAoiGeneration_;
        event.requestedGeneration = 0;
        event.requestedAoi = pendingRequestedAoi_;
        event.staleFramesDropped = staleFramesDroppedSinceAoi_;
        event.reason = update.measurement.diagnostic;
        emit aoiEvent(event);

        // Pair lost: return to full-frame locating. Reset the tracker so its
        // generation counter restarts from zero, matching the camera after
        // resetToFullFrame.
        tracker_.reset();
        initialLocateDone_ = false;
        aoiChangePending_ = false;
        expectedAoiGeneration_ = 0;
        staleFramesDroppedSinceAoi_ = 0;
        awaitingFirstValidAfterAoi_ = false;
        publishOverlayState(frame.sequence);
        if (config_.acquisition.enableHardwareAoi)
            emit fullFrameRequested();
        maybeEmitStatus(frame.sequence, frame.timestampNs, timestampSec,
                        TwoStarMeasurement{},
                        DifferentialSample{}, frame.configurationGeneration,
                        frame.sourceRect);
        return;
    }

    bool sampleAccepted = false;
    DifferentialSample sample;
    if (update.measurement.validPair) {
        sample = AtmosphereCalculator::makeSample(
            update.measurement, config_.optical, timestampSec);
        if (update.requestHardwareAoi) {
            if (config_.acquisition.enableHardwareAoi) {
                const std::uint64_t previousGeneration =
                    expectedAoiGeneration_;
                pendingTransitionId_ = ++nextTransitionId_;
                pendingPreviousGeneration_ = previousGeneration;
                pendingRequestedGeneration_ = update.requestGeneration;
                pendingRequestedAoi_ = update.requestedHardwareAoi;

                AoiEvent event;
                event.type = QStringLiteral("request");
                event.aoiEpoch = aoiEpoch_;
                event.transitionId = pendingTransitionId_;
                event.frameSequence = frame.sequence;
                event.frameTimestampSec = timestampSec;
                event.frameGeneration = frame.configurationGeneration;
                event.previousGeneration = previousGeneration;
                event.requestedGeneration = update.requestGeneration;
                event.requestedAoi = update.requestedHardwareAoi;
                event.starA = update.measurement.fullFrameStarA;
                event.starB = update.measurement.fullFrameStarB;
                event.longitudinalPx = sample.longitudinalPx;
                event.transversePx = sample.transversePx;
                event.differentialJumpPx = update.differentialJumpPx;
                event.differentialBaselineDeviationPx =
                    update.differentialBaselineDeviationPx;
                event.differentialBaselineViolationFrames =
                    update.differentialBaselineViolationFrames;
                event.validPair = update.measurement.validPair;
                event.reason = update.measurement.diagnostic;
                emit aoiEvent(event);

                // AOI movement request: ask for a new AOI and stop appending
                // samples until the new generation's frames arrive.
                aoiChangePending_ = true;
                expectedAoiGeneration_ = update.requestGeneration;
                staleFramesDroppedSinceAoi_ = 0;
                awaitingFirstValidAfterAoi_ = true;
                emit hardwareAoiRequested(update.requestedHardwareAoi,
                                          update.requestGeneration);
                maybeEmitStatus(frame.sequence, frame.timestampNs, timestampSec,
                                update.measurement, DifferentialSample{},
                                frame.configurationGeneration, frame.sourceRect);
            } else if (sample.valid) {
                // Software-only mode can immediately continue in the same
                // full-frame stream.
                sample.sequence = frame.sequence;
                calculator_.append(sample);
                ++validPairCount_;
                sampleAccepted = true;
                rateTimestampsNs_.push_back(frame.timestampNs);
                while (rateTimestampsNs_.size() > 100)
                    rateTimestampsNs_.pop_front();
                maybeEmitStatus(frame.sequence, frame.timestampNs, timestampSec,
                                update.measurement, sample,
                                frame.configurationGeneration, frame.sourceRect);
            }
        } else if (sample.valid) {
            // One differential sample per valid paired frame.
            sample.sequence = frame.sequence;
            calculator_.append(sample);
            ++validPairCount_;
            sampleAccepted = true;
            rateTimestampsNs_.push_back(frame.timestampNs);
            while (rateTimestampsNs_.size() > 100)
                rateTimestampsNs_.pop_front();
            maybeEmitStatus(frame.sequence, frame.timestampNs, timestampSec,
                           update.measurement,
                           sample, frame.configurationGeneration,
                           frame.sourceRect);
        }
    } else {
        maybeEmitStatus(frame.sequence, frame.timestampNs, timestampSec,
                       update.measurement,
                       DifferentialSample{}, frame.configurationGeneration,
                       frame.sourceRect);
    }

    if (sampleAccepted && awaitingFirstValidAfterAoi_) {
        AoiEvent event;
        event.type = QStringLiteral("first_valid_frame");
        event.aoiEpoch = aoiEpoch_;
        event.transitionId = activeTransitionId_;
        event.frameSequence = frame.sequence;
        event.frameTimestampSec = timestampSec;
        event.frameGeneration = frame.configurationGeneration;
        event.previousGeneration = activePreviousGeneration_;
        event.requestedGeneration = activeRequestedGeneration_;
        event.appliedGeneration = frame.configurationGeneration;
        event.staleFramesDropped = staleFramesDroppedSinceAoi_;
        event.requestedAoi = activeRequestedAoi_;
        event.appliedAoi = activeAppliedAoi_;
        event.starA = update.measurement.fullFrameStarA;
        event.starB = update.measurement.fullFrameStarB;
        event.longitudinalPx = sample.longitudinalPx;
        event.transversePx = sample.transversePx;
        event.validPair = update.measurement.validPair;
        event.sampleAccepted = true;
        event.validSampleCount = validPairCount_;
        event.reason = QStringLiteral("新 AOI 后首个已接收有效双星样本");
        emit aoiEvent(event);
        awaitingFirstValidAfterAoi_ = false;
    }

    maybeEmitAoiTransitionSample(frame, update, sample, sampleAccepted,
                                 timestampSec);

    // Overlay state is independent from the throttled image preview. Publish
    // it for every processed frame so valid centroids cannot disappear merely
    // because the preview interval has not elapsed yet.
    publishOverlayState(frame.sequence);
    publishDisplay(frame, update, lastResult_);
}

void MeasurementWorker::maybeEmitAoiTransitionSample(
    const CameraFrame &frame,
    const TrackerUpdate &update,
    const DifferentialSample &sample,
    bool sampleAccepted,
    double timestampSec)
{
    if (aoiTransitionDiagnosticFramesRemaining_ == 0)
        return;

    AoiTransitionSample diagnostic;
    diagnostic.aoiEpoch = aoiEpoch_;
    diagnostic.transitionId = activeTransitionId_;
    diagnostic.frameIndexAfterApply =
        static_cast<int>(++aoiTransitionDiagnosticFrameIndex_);
    diagnostic.frameSequence = frame.sequence;
    diagnostic.frameTimestampSec = timestampSec;
    diagnostic.frameGeneration = frame.configurationGeneration;
    diagnostic.sourceRect = frame.sourceRect;
    diagnostic.appliedAoi = activeAppliedAoi_;
    diagnostic.starA = update.measurement.fullFrameStarA;
    diagnostic.starB = update.measurement.fullFrameStarB;
    diagnostic.starAPeakIntensity = update.measurement.starA.peakIntensity;
    diagnostic.starBPeakIntensity = update.measurement.starB.peakIntensity;
    diagnostic.starAComponentAreaPx =
        update.measurement.starA.componentAreaPx;
    diagnostic.starBComponentAreaPx =
        update.measurement.starB.componentAreaPx;
    diagnostic.longitudinalPx = sample.longitudinalPx;
    diagnostic.transversePx = sample.transversePx;
    diagnostic.differentialJumpPx = update.differentialJumpPx;
    diagnostic.differentialBaselineDeviationPx =
        update.differentialBaselineDeviationPx;
    diagnostic.differentialBaselineViolationFrames =
        update.differentialBaselineViolationFrames;
    diagnostic.starAValid = update.measurement.starA.valid;
    diagnostic.starBValid = update.measurement.starB.valid;
    diagnostic.validPair = update.measurement.validPair;
    diagnostic.sampleAccepted = sampleAccepted;
    diagnostic.differentialContinuityRejected =
        update.differentialContinuityRejected;
    diagnostic.requestFullFrameRelocalization =
        update.requestFullFrameRelocalization;
    diagnostic.diagnostic = update.measurement.diagnostic;
    emit aoiTransitionSample(diagnostic);

    --aoiTransitionDiagnosticFramesRemaining_;
}

void MeasurementWorker::publishOverlayState(std::uint64_t sequence)
{
    DisplaySnapshot snapshot;
    snapshot.sequence = sequence;
    snapshot.overlay = tracker_.currentOverlay();
    snapshot.latestResult = lastResult_;
    displayMailbox_.publish(std::move(snapshot));
}

void MeasurementWorker::publishDisplay(const CameraFrame &frame,
                                       const TrackerUpdate &update,
                                       const MeasurementResult &result)
{
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const int intervalMs = qMax(
        1, qRound(1000.0 / config_.acquisition.previewRateHz));
    if (nowMs - lastPreviewMs_ < intervalMs)
        return;
    lastPreviewMs_ = nowMs;

    DisplaySnapshot snapshot;
    snapshot.sequence = frame.sequence;
    snapshot.overlay = update.overlay;
    snapshot.latestResult = result;

    displayMailbox_.publishAoiPatch(
        frame.mono8, frame.sourceRect,
        QSize(config_.acquisition.frameWidth, config_.acquisition.frameHeight),
        std::move(snapshot));
    emit starStateChanged(update.overlay);
}

void MeasurementWorker::maybeEmitStatus(std::uint64_t sequence,
                                        std::uint64_t frameTimestampNs,
                                        double timestampSec,
                                        const TwoStarMeasurement &stars,
                                        const DifferentialSample &sample,
                                        std::uint64_t configurationGeneration,
                                        const QRect &sourceRect)
{
    // Wall-clock heartbeat paces status refresh only. Result publication is
    // frame-count driven and must not be delayed by the GUI heartbeat.
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const bool heartbeatDue = nowMs - lastHeartbeatMs_ >= 500;

    // Result update schedule: first result after the r0 window fills, then by
    // elapsed camera-frame time. Using the configured target frame rate here
    // would make a 44.6 Hz camera update every 2.2 seconds for a 100-frame
    // target instead of respecting the configured one-second interval.
    const int r0Window = config_.processing.r0WindowFrames;
    bool scheduleDue = false;
    const bool recoveringWithValidSample =
        sample.valid && measurementTemporarilyInvalid_;
    if (!resultEmitted_)
        scheduleDue = (static_cast<long long>(validPairCount_) >= r0Window);
    else if (sample.valid && timestampSec > lastResultTimestampSec_)
        scheduleDue = timestampSec - lastResultTimestampSec_ >=
                      config_.processing.resultUpdateIntervalSec;
    if (recoveringWithValidSample && resultEmitted_ &&
        static_cast<long long>(validPairCount_) >= r0Window)
        scheduleDue = true;

    if (sample.valid)
        measurementTemporarilyInvalid_ = false;
    else
        measurementTemporarilyInvalid_ = true;
    if (scheduleDue) {
        resultEmitted_ = true;
        lastResultTimestampSec_ = timestampSec;
    }

    if (!heartbeatDue && !scheduleDue)
        return;

    if (heartbeatDue)
        lastHeartbeatMs_ = nowMs;

    const double measuredRateHz = currentRateHz(frameTimestampNs);
    AtmosphereResult atmo;
    if (sample.valid) {
        atmo = calculator_.calculate(sequence, measuredRateHz);
    } else {
        // A temporary tracking/AOI gap must not keep presenting the previous
        // valid parameter set.  Keep the rolling calculator window intact and
        // resume from it as soon as a new valid pair arrives.
        atmo.windowEndSequence = sequence;
        atmo.validSampleCount = calculator_.validSampleCount();
        atmo.measuredRateHz = measuredRateHz;
        atmo.statusMessage = stars.diagnostic.isEmpty()
            ? QStringLiteral("等待有效双星样本")
            : stars.diagnostic;
    }

    MeasurementResult result;
    result.sequence = sequence;
    result.timestampSec = timestampSec;
    result.configurationGeneration = configurationGeneration;
    result.aoiEpoch = aoiEpoch_;
    result.transitionId = activeTransitionId_;
    result.sourceRect = sourceRect;
    result.stars = stars;
    result.differential = sample;
    result.atmosphere = atmo;
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
