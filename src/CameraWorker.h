#pragma once

#include "AppConfig.h"
#include "DisplayMailbox.h"
#include "FrameQueue.h"
#include "PylonCamera.h"

#include <QObject>

class QTimer;

#include <atomic>

// Owns the pylon camera on the camera worker thread. Produces Mono8 frames into
// the measurement FrameQueue and throttled display snapshots into the
// DisplayMailbox. The camera callback never touches Qt widgets, never runs
// segmentation, and never writes files.
class CameraWorker final : public QObject {
    Q_OBJECT
public:
    explicit CameraWorker(FrameQueue &measurementQueue,
                          DisplayMailbox &displayMailbox,
                          QObject *parent = nullptr);

public slots:
    void configure(AppConfig config);
    void connectCamera();
    void disconnectCamera();
    void start();
    void stop();
    void applyHardwareAoi(RoiRect aoi, std::uint64_t generation);
    void requestFullFrame();
    void executeSoftwareTrigger();

signals:
    void cameraReady(CameraCapabilities capabilities);
    void cameraDisconnected();
    void cameraError(QString message);
    void cameraStatsUpdated(CameraStatistics stats);
    void frameReceived(std::uint64_t sequence);
    void hardwareAoiApplied(RoiRect aoi, std::uint64_t generation);
    void hardwareAoiFailed(QString message);
    // Emitted from stop() once grabbing has stopped and the camera is closed,
    // so the GUI can finish a run without calling QThread::wait().
    void stopped();

private:
    void onFrame(CameraFrame frame);

    FrameQueue &measurementQueue_;
    DisplayMailbox &displayMailbox_;
    PylonCamera camera_;
    AppConfig config_;
    QTimer *softwareTriggerTimer_ = nullptr;
    std::atomic_bool running_{false};
    qint64 lastFullFrameDisplayMs_ = 0;
    qint64 lastStatsEmitMs_ = 0;
};
