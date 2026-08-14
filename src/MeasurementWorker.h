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
    void roiStateChanged(RoiOverlay overlay);
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
                         const DifferentialSample &sample);
    double currentRateHz(std::uint64_t referenceTimestampNs) const;
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
    std::uint64_t validPairCount_ = 0;
    std::uint64_t validPairsSinceLastResult_ = 0;
    QTimer *pollTimer_ = nullptr;
    bool resultEmitted_ = false;
    qint64 lastHeartbeatMs_ = 0;
    qint64 lastPreviewMs_ = 0;
    std::deque<qint64> rateTimestampsNs_;
    MeasurementResult lastResult_;
};
