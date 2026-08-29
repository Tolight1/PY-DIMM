#include "CameraWorker.h"

#include <QDateTime>
#include <QTimer>

CameraWorker::CameraWorker(FrameQueue &measurementQueue,
                           DisplayMailbox &displayMailbox,
                           QObject *parent)
    : QObject(parent)
    , measurementQueue_(measurementQueue)
    , displayMailbox_(displayMailbox)
{
    camera_.setFrameCallback(
        [this](CameraFrame frame) { onFrame(std::move(frame)); });
    softwareTriggerTimer_ = new QTimer(this);
    softwareTriggerTimer_->setTimerType(Qt::PreciseTimer);
    connect(softwareTriggerTimer_, &QTimer::timeout,
            this, &CameraWorker::executeSoftwareTrigger);
}

void CameraWorker::configure(AppConfig config)
{
    config_ = config;
}

void CameraWorker::connectCamera()
{
    QString error;

    if (!camera_.openFirstCompatibleCamera(&error)) {
        emit cameraError(error);
        return;
    }
    if (!camera_.configureMono8(&error)) {
        emit cameraError(error);
        camera_.close();
        return;
    }

    emit cameraReady(camera_.capabilities());
}

void CameraWorker::disconnectCamera()
{
    stop();
    camera_.close();
    emit cameraDisconnected();
}

void CameraWorker::start()
{
    running_ = true;
    QString error;

    if (!camera_.isOpen()) {
        emit cameraError(QStringLiteral("请先连接相机，再开始采集"));
        running_ = false;
        return;
    }
    if (!camera_.configureExposure(config_.acquisition.exposureTimeMs, &error)) {
        emit cameraError(error);
        running_ = false;
        return;
    }
    if (config_.trigger.mode == TriggerMode::Continuous &&
        !camera_.configureAcquisitionRate(config_.acquisition.measurementRateHz,
                                          &error)) {
        emit cameraError(error);
    }
    if (!camera_.configureTrigger(config_.trigger, &error)) {
        emit cameraError(error);
        running_ = false;
        return;
    }
    // Start in full-frame mode. The measurement worker locates the two spots,
    // then requests one enclosing hardware AOI around their centroid windows.
    if (!camera_.resetToFullFrame(&error)) {
        emit cameraError(error);
        running_ = false;
        return;
    }
    if (!camera_.startGrabbing(&error)) {
        emit cameraError(error);
        running_ = false;
        return;
    }

    if (config_.trigger.mode == TriggerMode::Software) {
        const int intervalMs = qMax(
            1, qRound(1000.0 /
                      qMax(1.0, config_.acquisition.measurementRateHz)));
        softwareTriggerTimer_->start(intervalMs);
    }
    emit cameraReady(camera_.capabilities());
}

void CameraWorker::stop()
{
    const bool wasRunning = running_.exchange(false, std::memory_order_relaxed);
    if (softwareTriggerTimer_)
        softwareTriggerTimer_->stop();
    camera_.stopGrabbing();
    // Only close the measurement queue after the camera has stopped producing
    // frames, so no measurement frame is dropped before shutdown completes.
    measurementQueue_.close();
    if (wasRunning)
        emit stopped();
}

void CameraWorker::applyHardwareAoi(RoiRect aoi, std::uint64_t generation)
{
    Q_UNUSED(generation);
    QString error;
    RoiRect appliedAoi;
    if (!camera_.configureHardwareAoi(aoi, &error, &appliedAoi)) {
        QString recoveryError;
        camera_.resetToFullFrame(&recoveryError);
        camera_.startGrabbing(&recoveryError);
        emit cameraError(error);
        emit hardwareAoiFailed(error);
        return;
    }
    if (config_.trigger.mode == TriggerMode::Continuous &&
        !camera_.configureAcquisitionRate(config_.acquisition.measurementRateHz,
                                          &error)) {
        emit cameraError(error);
    }
    // Frames produced after this point are tagged with the new generation so
    // the measurement worker can discard frames from the previous AOI.  The
    // rectangle must be the camera readback, not the request: increment
    // alignment and camera-side clamping can change it.
    emit hardwareAoiApplied(appliedAoi, camera_.configurationGeneration());
}

void CameraWorker::requestFullFrame()
{
    QString error;
    if (!camera_.resetToFullFrame(&error)) {
        emit cameraError(error);
        return;
    }
    if (config_.trigger.mode == TriggerMode::Continuous &&
        !camera_.configureAcquisitionRate(config_.acquisition.measurementRateHz,
                                          &error)) {
        emit cameraError(error);
    }
    emit hardwareAoiApplied(camera_.activeHardwareAoi(),
                            camera_.configurationGeneration());
}

void CameraWorker::executeSoftwareTrigger()
{
    QString error;
    if (!camera_.executeSoftwareTrigger(&error))
        emit cameraError(error);
}

void CameraWorker::onFrame(CameraFrame frame)
{
    if (!running_.load(std::memory_order_relaxed))
        return;

    // Mono8 check. AOI frames still deliver Mono8 with their own sourceRect.
    if (!frame.isValid())
        return;

    const bool isFullFrame =
        frame.sourceRect ==
        QRect(0, 0, config_.acquisition.frameWidth, config_.acquisition.frameHeight);

    const qint64 now = QDateTime::currentMSecsSinceEpoch();

    // Full-frame display preview at the configured preview rate. Once the
    // hardware AOI is active the measurement worker patches the live AOI into
    // the latest full-frame canvas.
    if (isFullFrame) {
        const qint64 intervalMs = qMax<qint64>(
            qint64(1), qRound(1000.0 / config_.acquisition.previewRateHz));
        if (now - lastFullFrameDisplayMs_ >= intervalMs) {
            lastFullFrameDisplayMs_ = now;
            DisplaySnapshot snap;
            snap.fullFrameMono8 = frame.mono8.clone();
            snap.cameraStats = camera_.statistics();
            snap.sequence = frame.sequence;
            displayMailbox_.publish(std::move(snap));
        }
    }

    // The measurement queue receives every valid frame; it is not throttled to
    // a preview rate.
    const std::uint64_t sequence = frame.sequence;
    measurementQueue_.push(std::move(frame));

    if (now - lastStatsEmitMs_ >= 500) {
        lastStatsEmitMs_ = now;
        CameraStatistics stats = camera_.statistics();
        stats.queueDroppedFrames = measurementQueue_.droppedCount();
        emit cameraStatsUpdated(stats);
    }

    emit frameReceived(sequence);
}
