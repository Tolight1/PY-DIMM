#pragma once

#include "AppConfig.h"
#include "AtmosphereCalculator.h"
#include "DisplayMailbox.h"
#include "FrameQueue.h"
#include "TwoStarTracker.h"

#include <QObject>

#include <atomic>
#include <deque>

class QTimer;

class MeasurementWorker final : public QObject {
    Q_OBJECT
public:
    explicit MeasurementWorker(FrameQueue &queue,
                               DisplayMailbox &displayMailbox,
                               QObject *parent = nullptr);

public slots:
    void configure(AppConfig config);
    void start();
    void stop();
    void onHardwareAoiApplied(RoiRect aoi, std::uint64_t generation);
    void onHardwareAoiFailed(QString message);

signals:
    void hardwareAoiRequested(RoiRect aoi, std::uint64_t generation);
    void fullFrameRequested();
    void resultReady(MeasurementResult result);
    void measurementStatusReady(MeasurementResult result);
    void aoiEvent(AoiEvent event);
    void aoiTransitionSample(AoiTransitionSample sample);
    void starStateChanged(DisplayOverlay overlay);
    void measurementStatsUpdated(double measuredRateHz, std::uint64_t validPairs);
    void measurementError(QString message);
    // Emitted from stop() once the poll loop has stopped, so the GUI can
    // finish a run without calling QThread::wait().
    void stopped();

private:
    void poll();
    void processOneFrame(const CameraFrame &frame);
    void publishDisplay(const CameraFrame &frame,
                        const TrackerUpdate &update,
                        const MeasurementResult &result);
    void maybeEmitStatus(std::uint64_t sequence,
                         std::uint64_t frameTimestampNs,
                         double timestampSec,
                         const TwoStarMeasurement &stars,
                         const DifferentialSample &sample,
                         std::uint64_t configurationGeneration,
                         const QRect &sourceRect);
    void maybeEmitAoiTransitionSample(
        const CameraFrame &frame,
        const TrackerUpdate &update,
        const DifferentialSample &sample,
        bool sampleAccepted,
        double timestampSec);
    double currentRateHz(std::uint64_t referenceTimestampNs) const;
    void publishOverlayState(std::uint64_t sequence = 0);
    void resetState();

    FrameQueue &queue_;
    DisplayMailbox &displayMailbox_;
    AppConfig config_;
    TwoStarTracker tracker_;
    AtmosphereCalculator calculator_;
    std::atomic_bool running_{false};
    bool initialLocateDone_ = false;
    bool aoiChangePending_ = false;
    std::uint64_t expectedAoiGeneration_ = 0;
    std::uint64_t staleFramesDroppedSinceAoi_ = 0;
    bool awaitingFirstValidAfterAoi_ = false;
    std::uint64_t aoiEpoch_ = 0;
    std::uint64_t nextTransitionId_ = 0;
    std::uint64_t pendingTransitionId_ = 0;
    std::uint64_t pendingPreviousGeneration_ = 0;
    std::uint64_t pendingRequestedGeneration_ = 0;
    RoiRect pendingRequestedAoi_{0, 0, 0, 0};
    std::uint64_t activeTransitionId_ = 0;
    std::uint64_t activePreviousGeneration_ = 0;
    std::uint64_t activeRequestedGeneration_ = 0;
    RoiRect activeRequestedAoi_{0, 0, 0, 0};
    RoiRect activeAppliedAoi_{0, 0, 0, 0};
    std::uint64_t aoiTransitionDiagnosticFrameIndex_ = 0;
    std::uint64_t aoiTransitionDiagnosticFramesRemaining_ = 0;
    std::uint64_t validPairCount_ = 0;
    QTimer *pollTimer_ = nullptr;
    bool resultEmitted_ = false;
    bool measurementTemporarilyInvalid_ = false;
    double lastResultTimestampSec_ = 0.0;
    qint64 lastHeartbeatMs_ = 0;
    qint64 lastPreviewMs_ = 0;
    std::deque<qint64> rateTimestampsNs_;
    MeasurementResult lastResult_;
};
