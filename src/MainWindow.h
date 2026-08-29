#pragma once

#include "AppConfig.h"
#include "CameraWorker.h"
#include "DisplayMailbox.h"
#include "MeasurementWorker.h"
#include "ResultWriter.h"

#include <QMainWindow>
#include <QThread>

class QLabel;
class QProgressBar;
class QPlainTextEdit;
class QPushButton;
class QTimer;
class SettingsDialog;
class ZoomableImageView;

class MainWindow final : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    void onConnectClicked();
    void onDisconnectClicked();
    void onStartClicked();
    void onStopClicked();
    void onSettingsClicked();
    void refreshFullFramePreview();
    void refreshStatusSnapshot();
    void onResultReady(MeasurementResult result);
    void onMeasurementStatusReady(MeasurementResult result);
    void onCameraError(QString message);

private:
    void setupThreads();
    void stopThreads();
    void buildLayout();
    void updateControlsForState(bool running);
    void updateResultCards(const AtmosphereResult &result);
    void updateTimerIntervals();
    void appendLog(const QString &message);

    // Signal handlers from workers.
    void onCameraReady(CameraCapabilities capabilities);
    void onCameraDisconnected();
    void onCameraStatsUpdated(CameraStatistics stats);
    void onStarStateChanged(DisplayOverlay overlay);
    void onMeasurementStatsUpdated(double measuredRateHz, std::uint64_t validPairs);
    void onMeasurementError(QString message);
    void onWorkerStopped(bool cameraWorker);

    AppConfig config_;
    FrameQueue measurementQueue_;
    DisplayMailbox displayMailbox_;
    QThread cameraThread_;
    QThread measurementThread_;
    CameraWorker *cameraWorker_ = nullptr;
    MeasurementWorker *measurementWorker_ = nullptr;
    ResultWriter resultWriter_;
    QTimer *fullFrameTimer_ = nullptr;
    QTimer *statusTimer_ = nullptr;
    bool running_ = false;

    // Widget pointers created in buildLayout() with the plan's object names.
    ZoomableImageView *fullFrameImageView_ = nullptr;
    QLabel *starCentroidStatusLabel_ = nullptr;
    QLabel *hardwareAoiStatusLabel_ = nullptr;
    QLabel *cameraRateLabel_ = nullptr;
    QLabel *measurementRateLabel_ = nullptr;
    QLabel *validFrameCountLabel_ = nullptr;
    QProgressBar *windowProgressBar_ = nullptr;
    QLabel *r0ValueLabel_ = nullptr;
    QLabel *seeingValueLabel_ = nullptr;
    QLabel *theta0ValueLabel_ = nullptr;
    QLabel *tau0ValueLabel_ = nullptr;
    QLabel *measurementStateLabel_ = nullptr;
    QLabel *cameraStateLabel_ = nullptr;
    QLabel *acquisitionStateLabel_ = nullptr;
    QLabel *queueDroppedLabel_ = nullptr;
    QPushButton *settingsButton_ = nullptr;
    QPushButton *connectCameraButton_ = nullptr;
    QPushButton *disconnectCameraButton_ = nullptr;
    QPushButton *startAcquisitionButton_ = nullptr;
    QPushButton *stopAcquisitionButton_ = nullptr;
    QLabel *starTrackingStateLabel_ = nullptr;
    QLabel *droppedFramesLabel_ = nullptr;
    QLabel *lastErrorLabel_ = nullptr;
    QPlainTextEdit *logEdit_ = nullptr;

    // Latest worker state mirrored onto the GUI thread.
    CameraCapabilities lastCapabilities_;
    CameraStatistics lastCameraStats_;
    bool cameraReady_ = false;
    bool cameraStopped_ = false;
    bool measurementStopped_ = false;
};
