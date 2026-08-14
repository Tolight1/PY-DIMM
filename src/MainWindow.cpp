#include "MainWindow.h"
#include "ui_MainWindow.h"

#include "ImageDisplayAdapter.h"
#include "SettingsDialog.h"

#include <QDateTime>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QScrollBar>
#include <QStringList>
#include <QSplitter>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , config_(AppConfig::defaults())
{
    QSettings settings;
    AppConfig loaded = AppConfig::defaults();
    loaded.load(settings);
    if (loaded.validate().isEmpty())
        config_ = loaded;

    Ui::MainWindow ui;
    ui.setupUi(this);
    buildLayout();

    setupThreads();

    connect(connectCameraButton_, &QPushButton::clicked, this,
            &MainWindow::onConnectClicked);
    connect(disconnectCameraButton_, &QPushButton::clicked, this,
            &MainWindow::onDisconnectClicked);
    connect(startAcquisitionButton_, &QPushButton::clicked, this,
            &MainWindow::onStartClicked);
    connect(stopAcquisitionButton_, &QPushButton::clicked, this,
            &MainWindow::onStopClicked);
    connect(settingsButton_, &QPushButton::clicked, this,
            &MainWindow::onSettingsClicked);

    fullFrameTimer_ = new QTimer(this);
    connect(fullFrameTimer_, &QTimer::timeout, this,
            &MainWindow::refreshFullFramePreview);
    roiTimer_ = new QTimer(this);
    connect(roiTimer_, &QTimer::timeout, this, &MainWindow::refreshRoiPreview);
    statusTimer_ = new QTimer(this);
    connect(statusTimer_, &QTimer::timeout, this,
            &MainWindow::refreshStatusSnapshot);

    updateControlsForState(false);
    appendLog(tr("KY-DIMM 已就绪（构建和实际相机行为尚待硬件确认）"));
}

MainWindow::~MainWindow()
{
    // Stop workers synchronously so grabbing is finished before the threads
    // quit. BlockingQueuedConnection is safe here: stop() stops grabbing first
    // and the threads are still running their event loops.
    if (cameraWorker_ && cameraThread_.isRunning())
        QMetaObject::invokeMethod(cameraWorker_, "stop",
                                  Qt::BlockingQueuedConnection);
    if (measurementWorker_ && measurementThread_.isRunning())
        QMetaObject::invokeMethod(measurementWorker_, "stop",
                                  Qt::BlockingQueuedConnection);
    resultWriter_.finishRun();

    cameraThread_.quit();
    measurementThread_.quit();
    cameraThread_.wait(2000);
    measurementThread_.wait(2000);

    cameraWorker_ = nullptr;
    measurementWorker_ = nullptr;
}

void MainWindow::setupThreads()
{
    cameraThread_.setObjectName(QStringLiteral("CameraWorkerThread"));
    measurementThread_.setObjectName(QStringLiteral("MeasurementWorkerThread"));

    cameraWorker_ = new CameraWorker(measurementQueue_, displayMailbox_);
    cameraWorker_->moveToThread(&cameraThread_);

    measurementWorker_ = new MeasurementWorker(measurementQueue_,
                                               displayMailbox_);
    measurementWorker_->moveToThread(&measurementThread_);

    // Workers belong to their worker threads. Defer destruction to the owning
    // thread instead of deleting them from the GUI thread after wait().
    connect(&cameraThread_, &QThread::finished,
            cameraWorker_, &QObject::deleteLater);
    connect(&measurementThread_, &QThread::finished,
            measurementWorker_, &QObject::deleteLater);

    // Workers -> GUI (queued automatically because the sender thread differs).
    connect(cameraWorker_, &CameraWorker::cameraReady, this,
            &MainWindow::onCameraReady);
    connect(cameraWorker_, &CameraWorker::cameraError, this,
            &MainWindow::onCameraError);
    connect(cameraWorker_, &CameraWorker::cameraDisconnected, this,
            &MainWindow::onCameraDisconnected);
    connect(cameraWorker_, &CameraWorker::cameraStatsUpdated, this,
            &MainWindow::onCameraStatsUpdated);
    connect(cameraWorker_, &CameraWorker::stopped, this,
            [this]() { onWorkerStopped(true); });
    connect(measurementWorker_, &MeasurementWorker::resultReady, this,
            &MainWindow::onResultReady);
    connect(measurementWorker_, &MeasurementWorker::measurementStatusReady,
            this, &MainWindow::onMeasurementStatusReady);
    connect(measurementWorker_, &MeasurementWorker::roiStateChanged, this,
            &MainWindow::onRoiStateChanged);
    connect(measurementWorker_, &MeasurementWorker::measurementStatsUpdated,
            this, &MainWindow::onMeasurementStatsUpdated);
    connect(measurementWorker_, &MeasurementWorker::measurementError, this,
            &MainWindow::onMeasurementError);
    connect(measurementWorker_, &MeasurementWorker::stopped, this,
            [this]() { onWorkerStopped(false); });

    // Measurement worker -> camera worker: hardware AOI handshake.
    connect(measurementWorker_, &MeasurementWorker::hardwareAoiRequested,
            cameraWorker_, &CameraWorker::applyHardwareAoi,
            Qt::QueuedConnection);
    connect(measurementWorker_, &MeasurementWorker::fullFrameRequested,
            cameraWorker_, &CameraWorker::requestFullFrame,
            Qt::QueuedConnection);
    connect(cameraWorker_, &CameraWorker::hardwareAoiApplied,
            measurementWorker_, &MeasurementWorker::onHardwareAoiApplied,
            Qt::QueuedConnection);
    connect(cameraWorker_, &CameraWorker::hardwareAoiFailed,
            measurementWorker_, &MeasurementWorker::onHardwareAoiFailed,
            Qt::QueuedConnection);

    cameraThread_.start();
    measurementThread_.start();
}

void MainWindow::stopThreads()
{
    if (cameraWorker_)
        QMetaObject::invokeMethod(cameraWorker_, "stop", Qt::QueuedConnection);
    if (measurementWorker_)
        QMetaObject::invokeMethod(measurementWorker_, "stop",
                                  Qt::QueuedConnection);
}

void MainWindow::onConnectClicked()
{
    if (cameraReady_)
        return;
    QMetaObject::invokeMethod(cameraWorker_, "configure", Qt::QueuedConnection,
                              Q_ARG(AppConfig, config_));
    QMetaObject::invokeMethod(cameraWorker_, "connectCamera",
                              Qt::QueuedConnection);
    cameraStateLabel_->setText(tr("相机：正在连接"));
    connectCameraButton_->setEnabled(false);
    appendLog(tr("正在连接相机"));
}

void MainWindow::onDisconnectClicked()
{
    if (running_)
        onStopClicked();
    QMetaObject::invokeMethod(cameraWorker_, "disconnectCamera",
                              Qt::QueuedConnection);
    cameraStateLabel_->setText(tr("相机：正在断开"));
    disconnectCameraButton_->setEnabled(false);
    appendLog(tr("正在断开相机"));
}

void MainWindow::onStartClicked()
{
    const QStringList errors = config_.validate();
    if (!errors.isEmpty()) {
        QMessageBox::warning(this, tr("参数无效"),
                             errors.join(QLatin1Char('\n')));
        return;
    }
    if (running_)
        return;
    if (!cameraReady_) {
        QMessageBox::information(this, tr("相机未连接"),
                                 tr("请先点击“连接相机”，确认相机已就绪后再开始采集。"));
        return;
    }

    // Reset shared state before (re)starting.
    measurementQueue_.reopen();
    measurementQueue_.clear();
    displayMailbox_.clear();

    running_ = true;
    cameraStopped_ = false;
    measurementStopped_ = false;

    QMetaObject::invokeMethod(cameraWorker_, "configure", Qt::QueuedConnection,
                              Q_ARG(AppConfig, config_));
    QMetaObject::invokeMethod(measurementWorker_, "configure",
                              Qt::QueuedConnection, Q_ARG(AppConfig, config_));
    QMetaObject::invokeMethod(cameraWorker_, "start", Qt::QueuedConnection);
    QMetaObject::invokeMethod(measurementWorker_, "start",
                              Qt::QueuedConnection);

    updateTimerIntervals();
    fullFrameTimer_->start();
    roiTimer_->start();
    statusTimer_->start();

    updateControlsForState(true);
    acquisitionStateLabel_->setText(tr("采集状态：运行中"));
    appendLog(tr("开始采集"));
}

void MainWindow::onStopClicked()
{
    if (!running_)
        return;
    running_ = false;
    stopAcquisitionButton_->setEnabled(false);
    acquisitionStateLabel_->setText(tr("采集状态：正在停止"));
    appendLog(tr("正在停止采集"));
    stopThreads();
    // Completion is handled by onWorkerStopped once both workers report.
}

void MainWindow::onSettingsClicked()
{
    SettingsDialog dialog(config_, this);
    connect(&dialog, &SettingsDialog::configApplied, this,
            [this](const AppConfig &config) {
                config_ = config;
                if (running_) {
                    appendLog(tr("采集正在运行，新参数将在下次启动时生效"));
                }
                updateTimerIntervals();
            });
    dialog.exec();
}

void MainWindow::refreshFullFramePreview()
{
    DisplaySnapshot snap;
    if (!displayMailbox_.tryTake(snap))
        return;
    if (snap.fullFrameMono8.empty())
        return;
    QImage image = ImageDisplayAdapter::toGrayImage(snap.fullFrameMono8);
    if (image.isNull())
        return;
    image = ImageDisplayAdapter::drawOverlay(image, snap.overlay,
                                             image.rect());
    fullFrameImageLabel_->setPixmap(QPixmap::fromImage(image));
}

void MainWindow::refreshRoiPreview()
{
    DisplaySnapshot snap;
    if (!displayMailbox_.tryTake(snap))
        return;
    if (!snap.roiAMono8.empty()) {
        const QImage image = ImageDisplayAdapter::toGrayImage(snap.roiAMono8);
        if (!image.isNull())
            roiAImageLabel_->setPixmap(QPixmap::fromImage(image));
    }
    if (!snap.roiBMono8.empty()) {
        const QImage image = ImageDisplayAdapter::toGrayImage(snap.roiBMono8);
        if (!image.isNull())
            roiBImageLabel_->setPixmap(QPixmap::fromImage(image));
    }
}

void MainWindow::refreshStatusSnapshot()
{
    // Camera counters come from the camera worker's own signal; the mailbox may
    // not carry them while the measurement worker owns the display snapshots.
    droppedFramesLabel_->setText(
        tr("相机丢帧：%1").arg(lastCameraStats_.droppedFrames));
    queueDroppedLabel_->setText(
        tr("队列丢帧：%1").arg(lastCameraStats_.queueDroppedFrames));
}

void MainWindow::onResultReady(MeasurementResult result)
{
    updateResultCards(result.atmosphere);
    resultWriter_.append(result);
}

void MainWindow::onMeasurementStatusReady(MeasurementResult result)
{
    updateResultCards(result.atmosphere);
}

void MainWindow::onCameraReady(CameraCapabilities capabilities)
{
    cameraReady_ = true;
    lastCapabilities_ = capabilities;
    cameraStateLabel_->setText(
        tr("相机：%1 / %2").arg(capabilities.modelName,
                               capabilities.serialNumber));
    updateControlsForState(running_);
    appendLog(tr("相机已就绪：%1（Mono8，1920 × 1200）").arg(capabilities.modelName));

    if (running_ && !resultWriter_.isRunning()) {
        QStringList capabilityErrors;
        if (!capabilities.supportsMono8)
            capabilityErrors << tr("相机不支持 Mono8");
        if (config_.trigger.mode == TriggerMode::Software &&
            !capabilities.supportsSoftwareTrigger)
            capabilityErrors << tr("相机不支持软件触发");
        if (config_.trigger.mode == TriggerMode::Hardware &&
            !capabilities.supportsHardwareTrigger)
            capabilityErrors << tr("相机不支持硬件触发");
        if (config_.acquisition.enableHardwareAoi &&
            !capabilities.supportsHardwareAoi)
            capabilityErrors << tr("相机不支持硬件 AOI");
        if (!capabilityErrors.isEmpty()) {
            QMessageBox::warning(this, tr("相机能力不满足配置"),
                                 capabilityErrors.join(QLatin1Char('\n')));
            running_ = false;
            fullFrameTimer_->stop();
            roiTimer_->stop();
            statusTimer_->stop();
            updateControlsForState(false);
            stopThreads();
            return;
        }

        QString error;
        if (!resultWriter_.startRun(config_, capabilities, &error)) {
            QMessageBox::warning(this, tr("无法开始数据记录"), error);
            running_ = false;
            fullFrameTimer_->stop();
            roiTimer_->stop();
            statusTimer_->stop();
            updateControlsForState(false);
            stopThreads();
        }
    }
}

void MainWindow::onCameraDisconnected()
{
    cameraReady_ = false;
    lastCapabilities_ = CameraCapabilities{};
    cameraStateLabel_->setText(tr("相机：未连接"));
    acquisitionStateLabel_->setText(tr("采集状态：已停止"));
    updateControlsForState(false);
    appendLog(tr("相机已断开"));
}

void MainWindow::onCameraStatsUpdated(CameraStatistics stats)
{
    lastCameraStats_ = stats;
    if (cameraReady_ && !stats.lastError.isEmpty()) {
        cameraStateLabel_->setText(tr("相机错误：%1").arg(stats.lastError));
    }
    resultWriter_.appendCameraStats(stats);
}

void MainWindow::onRoiStateChanged(RoiOverlay overlay)
{
    if (overlay.hasRois) {
        starAStatusLabel_->setText(
            tr("StarA：(%1, %2)")
                .arg(overlay.starA.x(), 0, 'f', 1)
                .arg(overlay.starA.y(), 0, 'f', 1));
        starBStatusLabel_->setText(
            tr("StarB：(%1, %2)")
                .arg(overlay.starB.x(), 0, 'f', 1)
                .arg(overlay.starB.y(), 0, 'f', 1));
        roiTrackingStateLabel_->setText(tr("ROI 跟踪：跟踪中"));
    } else {
        starAStatusLabel_->setText(tr("StarA：未定位"));
        starBStatusLabel_->setText(tr("StarB：未定位"));
        roiTrackingStateLabel_->setText(tr("ROI 跟踪：定位中"));
    }

    if (overlay.hasHardwareAoi) {
        hardwareAoiStatusLabel_->setText(
                tr("AOI：%1 × %2，位置 (%3, %4)")
                .arg(overlay.hardwareAoi.width)
                .arg(overlay.hardwareAoi.height)
                .arg(overlay.hardwareAoi.x)
                .arg(overlay.hardwareAoi.y));
    } else {
        hardwareAoiStatusLabel_->setText(tr("AOI：全画幅"));
    }
}

void MainWindow::onMeasurementStatsUpdated(double measuredRateHz,
                                           std::uint64_t validPairs)
{
    const double required = config_.acquisition.measurementRateHz;
    const bool gatePassed = measuredRateHz >= required;
    measurementRateLabel_->setText(
        tr("实际测量率：%1 Hz").arg(measuredRateHz, 0, 'f', 1));
    measurementRateLabel_->setStyleSheet(
        gatePassed ? QStringLiteral("color:#33d17a;")
                   : QStringLiteral("color:#ffb454;"));
    const int windowFrames = config_.processing.r0WindowFrames;
    const int windowPairs = static_cast<int>(qMin<std::uint64_t>(
        validPairs, static_cast<std::uint64_t>(windowFrames)));
    windowProgressBar_->setRange(0, windowFrames);
    windowProgressBar_->setValue(windowPairs);
    validFrameCountLabel_->setText(
        tr("有效样本：%1 / %2").arg(windowPairs).arg(windowFrames));
}

void MainWindow::onMeasurementError(QString message)
{
    appendLog(tr("测量错误：%1").arg(message));
    lastErrorLabel_->setText(message);
}

void MainWindow::onCameraError(QString message)
{
    appendLog(tr("相机错误：%1").arg(message));
    lastErrorLabel_->setText(message);

    if (!cameraReady_ && running_) {
        // A camera failure before cameraReady means startup failed: tear down
        // the run. Failures after cameraReady are transient and only logged.
        running_ = false;
        fullFrameTimer_->stop();
        roiTimer_->stop();
        statusTimer_->stop();
        resultWriter_.finishRun();
        updateControlsForState(false);
        stopThreads();
    } else {
        cameraStateLabel_->setText(tr("相机错误：%1").arg(message));
        if (!cameraReady_ && !running_)
            updateControlsForState(false);
    }
}

void MainWindow::onWorkerStopped(bool cameraWorker)
{
    if (cameraWorker)
        cameraStopped_ = true;
    else
        measurementStopped_ = true;

    if (cameraStopped_ && measurementStopped_) {
        cameraStopped_ = false;
        measurementStopped_ = false;
        resultWriter_.finishRun();
        fullFrameTimer_->stop();
        roiTimer_->stop();
        statusTimer_->stop();
        updateControlsForState(false);
        acquisitionStateLabel_->setText(tr("采集状态：已停止"));
        measurementRateLabel_->setText(tr("实际测量率：-- Hz"));
        measurementRateLabel_->setStyleSheet(QString());
        appendLog(tr("采集已停止"));
    }
}

void MainWindow::updateResultCards(const AtmosphereResult &a)
{
    const QString dimStyle = QStringLiteral("color:#5c6e86;");
    if (a.valid) {
        r0ValueLabel_->setText(QString::number(a.r0ZenithM, 'f', 3));
        seeingValueLabel_->setText(QString::number(a.seeingArcsec, 'f', 2));
        theta0ValueLabel_->setText(QString::number(a.theta0Arcsec, 'f', 2));
        if (!a.tau0Valid) {
            tau0ValueLabel_->setText(QStringLiteral("--"));
        } else if (a.underResolved) {
            tau0ValueLabel_->setText(
                QStringLiteral("< %1").arg(a.tau0ResolutionMs, 0, 'f', 2));
        } else {
            tau0ValueLabel_->setText(QString::number(a.tau0Ms, 'f', 2));
        }
        r0ValueLabel_->setStyleSheet(QStringLiteral("color:#33d17a;"));
        seeingValueLabel_->setStyleSheet(QStringLiteral("color:#33d17a;"));
        theta0ValueLabel_->setStyleSheet(QStringLiteral("color:#33d17a;"));
        tau0ValueLabel_->setStyleSheet(
            a.tau0Valid ? QStringLiteral("color:#33d17a;")
                        : QStringLiteral("color:#ffb454;"));
    } else {
        r0ValueLabel_->setText(QStringLiteral("--"));
        seeingValueLabel_->setText(QStringLiteral("--"));
        theta0ValueLabel_->setText(QStringLiteral("--"));
        tau0ValueLabel_->setText(QStringLiteral("--"));
        r0ValueLabel_->setStyleSheet(dimStyle);
        seeingValueLabel_->setStyleSheet(dimStyle);
        theta0ValueLabel_->setStyleSheet(dimStyle);
        tau0ValueLabel_->setStyleSheet(dimStyle);
    }

    measurementStateLabel_->setText(
        a.statusMessage.isEmpty() ? tr("未启动") : a.statusMessage);

    const int windowFrames = config_.processing.r0WindowFrames;
    windowProgressBar_->setRange(0, windowFrames);
    windowProgressBar_->setValue(
        qMin(a.validSampleCount, windowFrames));
    validFrameCountLabel_->setText(
        tr("有效样本：%1 / %2").arg(a.validSampleCount).arg(windowFrames));
}

void MainWindow::updateControlsForState(bool running)
{
    startAcquisitionButton_->setEnabled(cameraReady_ && !running);
    stopAcquisitionButton_->setEnabled(running);
    connectCameraButton_->setEnabled(!cameraReady_ && !running);
    disconnectCameraButton_->setEnabled(cameraReady_ && !running);
    // Settings may be opened while grabbing. Changes are applied to the next
    // acquisition start; preview timers can still be refreshed immediately.
    settingsButton_->setEnabled(true);
}

void MainWindow::updateTimerIntervals()
{
    fullFrameTimer_->setInterval(qMax(
        1, qRound(1000.0 / config_.acquisition.fullFramePreviewRateHz)));
    roiTimer_->setInterval(
        qMax(1, qRound(1000.0 / config_.acquisition.roiPreviewRateHz)));
    statusTimer_->setInterval(400);
}

void MainWindow::appendLog(const QString &message)
{
    if (!logEdit_)
        return;
    logEdit_->appendPlainText(QDateTime::currentDateTime().toString(
                                  QStringLiteral("HH:mm:ss.zzz ")) +
                              message);
    if (QScrollBar *bar = logEdit_->verticalScrollBar())
        bar->setValue(bar->maximum());
}

void MainWindow::buildLayout()
{
    QWidget *central = centralWidget();
    if (!central) {
        central = new QWidget(this);
        setCentralWidget(central);
    }

    auto *rootLayout = new QVBoxLayout(central);
    rootLayout->setContentsMargins(16, 12, 16, 12);
    rootLayout->setSpacing(12);

    // Top bar: title, camera state, measurement rate, acquisition state,
    // settings, start and stop buttons.
    auto *topBar = new QHBoxLayout;
    topBar->setContentsMargins(4, 0, 4, 0);
    topBar->setSpacing(8);
    auto *titleLabel = new QLabel(tr("KY-DIMM"), central);
    titleLabel->setObjectName(QStringLiteral("windowTitleLabel"));
    topBar->addWidget(titleLabel);
    auto *subtitleLabel = new QLabel(
        QStringLiteral("双星差分测量 · 实时观测控制台"), central);
    subtitleLabel->setObjectName(QStringLiteral("windowSubtitleLabel"));
    topBar->addWidget(subtitleLabel);

    cameraStateLabel_ = new QLabel(tr("相机：未连接"), central);
    cameraStateLabel_->setObjectName(QStringLiteral("cameraStateLabel"));
    cameraStateLabel_->setProperty("statusBadge", true);
    topBar->addWidget(cameraStateLabel_);

    measurementRateLabel_ = new QLabel(tr("实际测量率：-- Hz"), central);
    measurementRateLabel_->setObjectName(QStringLiteral("measurementRateLabel"));
    measurementRateLabel_->setProperty("statusBadge", true);
    topBar->addWidget(measurementRateLabel_);

    acquisitionStateLabel_ = new QLabel(tr("采集状态：已停止"), central);
    acquisitionStateLabel_->setObjectName(QStringLiteral("acquisitionStateLabel"));
    acquisitionStateLabel_->setProperty("statusBadge", true);
    topBar->addWidget(acquisitionStateLabel_);

    topBar->addStretch();

    connectCameraButton_ = new QPushButton(tr("连接相机"), central);
    connectCameraButton_->setObjectName(QStringLiteral("connectCameraButton"));
    disconnectCameraButton_ = new QPushButton(tr("断开相机"), central);
    disconnectCameraButton_->setObjectName(QStringLiteral("disconnectCameraButton"));
    settingsButton_ = new QPushButton(tr("设置"), central);
    settingsButton_->setObjectName(QStringLiteral("settingsButton"));
    startAcquisitionButton_ = new QPushButton(tr("开始采集"), central);
    startAcquisitionButton_->setObjectName(
        QStringLiteral("startAcquisitionButton"));
    stopAcquisitionButton_ = new QPushButton(tr("停止采集"), central);
    stopAcquisitionButton_->setObjectName(
        QStringLiteral("stopAcquisitionButton"));
    topBar->addWidget(connectCameraButton_);
    topBar->addWidget(disconnectCameraButton_);
    topBar->addWidget(settingsButton_);
    topBar->addWidget(startAcquisitionButton_);
    topBar->addWidget(stopAcquisitionButton_);
    rootLayout->addLayout(topBar);

    // Central splitter: full-frame preview on the left, result cards and ROI
    // previews on the right.
    auto *splitter = new QSplitter(Qt::Horizontal, central);
    splitter->setObjectName(QStringLiteral("mainSplitter"));
    splitter->setHandleWidth(8);

    auto *previewFrame = new QFrame(splitter);
    previewFrame->setProperty("panel", true);
    previewFrame->setProperty("panelRole", "canvas");
    auto *previewLayout = new QVBoxLayout(previewFrame);
    previewLayout->setContentsMargins(14, 12, 14, 12);
    previewLayout->setSpacing(8);
    auto *previewTitle = new QLabel(QStringLiteral("全画幅观测"), previewFrame);
    previewTitle->setProperty("sectionTitle", true);
    previewLayout->addWidget(previewTitle);
    auto *previewMeta = new QLabel(
        QStringLiteral("1920 × 1200 · Mono8 · 实时画面"), previewFrame);
    previewMeta->setProperty("sectionMeta", true);
    previewLayout->addWidget(previewMeta);
    fullFrameImageLabel_ = new QLabel(previewFrame);
    fullFrameImageLabel_->setObjectName(QStringLiteral("fullFrameImageLabel"));
    fullFrameImageLabel_->setAlignment(Qt::AlignCenter);
    fullFrameImageLabel_->setMinimumSize(640, 400);
    fullFrameImageLabel_->setText(tr("全画幅 1920 × 1200"));
    fullFrameImageLabel_->setScaledContents(true);
    previewLayout->addWidget(fullFrameImageLabel_);
    splitter->addWidget(previewFrame);

    auto *rightPanel = new QFrame(splitter);
    rightPanel->setProperty("panel", true);
    rightPanel->setProperty("panelRole", "result");
    auto *rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(12, 12, 12, 12);
    rightLayout->setSpacing(10);
    auto *resultTitle = new QLabel(QStringLiteral("测量结果"), rightPanel);
    resultTitle->setProperty("sectionTitle", true);
    rightLayout->addWidget(resultTitle);

    // Four-parameter result cards.
    auto *resultCard = new QFrame(rightPanel);
    resultCard->setProperty("panel", true);
    resultCard->setProperty("panelRole", "result");
    auto *resultLayout = new QGridLayout(resultCard);
    resultLayout->setContentsMargins(14, 12, 14, 12);
    resultLayout->setHorizontalSpacing(14);
    resultLayout->setVerticalSpacing(6);
    const QStringList cardTitles = {QStringLiteral("r0"),
                                    QStringLiteral("seeing"),
                                    QStringLiteral("theta0"),
                                    QStringLiteral("tau0")};
    for (int col = 0; col < cardTitles.size(); ++col) {
        auto *title = new QLabel(cardTitles.at(col), resultCard);
        title->setProperty("cardTitle", true);
        resultLayout->addWidget(title, 0, col);
    }

    r0ValueLabel_ = new QLabel(QStringLiteral("--"), resultCard);
    r0ValueLabel_->setObjectName(QStringLiteral("r0ValueLabel"));
    r0ValueLabel_->setProperty("resultValue", true);
    seeingValueLabel_ = new QLabel(QStringLiteral("--"), resultCard);
    seeingValueLabel_->setObjectName(QStringLiteral("seeingValueLabel"));
    seeingValueLabel_->setProperty("resultValue", true);
    theta0ValueLabel_ = new QLabel(QStringLiteral("--"), resultCard);
    theta0ValueLabel_->setObjectName(QStringLiteral("theta0ValueLabel"));
    theta0ValueLabel_->setProperty("resultValue", true);
    tau0ValueLabel_ = new QLabel(QStringLiteral("--"), resultCard);
    tau0ValueLabel_->setObjectName(QStringLiteral("tau0ValueLabel"));
    tau0ValueLabel_->setProperty("resultValue", true);
    resultLayout->addWidget(r0ValueLabel_, 1, 0);
    resultLayout->addWidget(seeingValueLabel_, 1, 1);
    resultLayout->addWidget(theta0ValueLabel_, 1, 2);
    resultLayout->addWidget(tau0ValueLabel_, 1, 3);

    auto *r0Unit = new QLabel(QStringLiteral("m"), resultCard);
    auto *seeingUnit = new QLabel(QStringLiteral("arcsec"), resultCard);
    auto *thetaUnit = new QLabel(QStringLiteral("arcsec"), resultCard);
    auto *tauUnit = new QLabel(QStringLiteral("ms"), resultCard);
    for (QLabel *unit : {r0Unit, seeingUnit, thetaUnit, tauUnit})
        unit->setProperty("resultUnit", true);
    resultLayout->addWidget(r0Unit, 2, 0);
    resultLayout->addWidget(seeingUnit, 2, 1);
    resultLayout->addWidget(thetaUnit, 2, 2);
    resultLayout->addWidget(tauUnit, 2, 3);

    validFrameCountLabel_ = new QLabel(tr("有效样本：0"), resultCard);
    validFrameCountLabel_->setObjectName(QStringLiteral("validFrameCountLabel"));
    resultLayout->addWidget(validFrameCountLabel_, 3, 0, 1, 2);
    windowProgressBar_ = new QProgressBar(resultCard);
    windowProgressBar_->setObjectName(QStringLiteral("windowProgressBar"));
    windowProgressBar_->setRange(0, 1000);
    resultLayout->addWidget(windowProgressBar_, 3, 2, 1, 2);
    measurementStateLabel_ = new QLabel(tr("未启动"), resultCard);
    measurementStateLabel_->setObjectName(
        QStringLiteral("measurementStateLabel"));
    resultLayout->addWidget(measurementStateLabel_, 4, 0, 1, 4);
    rightLayout->addWidget(resultCard);

    // ROI A and ROI B previews.
    auto *roiTitle = new QLabel(QStringLiteral("目标 ROI 与质心状态"), rightPanel);
    roiTitle->setProperty("sectionTitle", true);
    rightLayout->addWidget(roiTitle);
    auto *roiRow = new QHBoxLayout;
    roiRow->setSpacing(10);
    auto *roiAFrame = new QFrame(rightPanel);
    roiAFrame->setProperty("panel", true);
    roiAFrame->setProperty("panelRole", "roi");
    auto *roiALayout = new QVBoxLayout(roiAFrame);
    roiALayout->setContentsMargins(8, 8, 8, 8);
    roiALayout->setSpacing(6);
    roiAImageLabel_ = new QLabel(roiAFrame);
    roiAImageLabel_->setObjectName(QStringLiteral("roiAImageLabel"));
    roiAImageLabel_->setAlignment(Qt::AlignCenter);
    roiAImageLabel_->setMinimumSize(160, 160);
    roiAImageLabel_->setText(tr("ROI A 64 × 64"));
    roiAImageLabel_->setScaledContents(true);
    starAStatusLabel_ = new QLabel(tr("StarA：未定位"), roiAFrame);
    starAStatusLabel_->setObjectName(QStringLiteral("starAStatusLabel"));
    roiALayout->addWidget(roiAImageLabel_);
    roiALayout->addWidget(starAStatusLabel_);
    roiRow->addWidget(roiAFrame);

    auto *roiBFrame = new QFrame(rightPanel);
    roiBFrame->setProperty("panel", true);
    roiBFrame->setProperty("panelRole", "roi");
    auto *roiBLayout = new QVBoxLayout(roiBFrame);
    roiBLayout->setContentsMargins(8, 8, 8, 8);
    roiBLayout->setSpacing(6);
    roiBImageLabel_ = new QLabel(roiBFrame);
    roiBImageLabel_->setObjectName(QStringLiteral("roiBImageLabel"));
    roiBImageLabel_->setAlignment(Qt::AlignCenter);
    roiBImageLabel_->setMinimumSize(160, 160);
    roiBImageLabel_->setText(tr("ROI B 64 × 64"));
    roiBImageLabel_->setScaledContents(true);
    starBStatusLabel_ = new QLabel(tr("StarB：未定位"), roiBFrame);
    starBStatusLabel_->setObjectName(QStringLiteral("starBStatusLabel"));
    roiBLayout->addWidget(roiBImageLabel_);
    roiBLayout->addWidget(starBStatusLabel_);
    roiRow->addWidget(roiBFrame);
    rightLayout->addLayout(roiRow);
    rightLayout->addStretch();

    splitter->addWidget(rightPanel);
    splitter->setStretchFactor(0, 58);
    splitter->setStretchFactor(1, 42);
    rootLayout->addWidget(splitter, 1);

    // Bottom diagnostic surface: log, ROI/AOI status, drop counters, last error.
    auto *diagnosticFrame = new QFrame(central);
    diagnosticFrame->setProperty("panel", true);
    diagnosticFrame->setProperty("panelRole", "diagnostic");
    auto *diagnosticLayout = new QHBoxLayout(diagnosticFrame);
    diagnosticLayout->setContentsMargins(12, 10, 12, 10);
    diagnosticLayout->setSpacing(12);

    auto *logBlock = new QWidget(diagnosticFrame);
    auto *logLayout = new QVBoxLayout(logBlock);
    logLayout->setContentsMargins(0, 0, 0, 0);
    logLayout->setSpacing(4);
    auto *logTitle = new QLabel(QStringLiteral("运行日志"), logBlock);
    logTitle->setProperty("sectionTitle", true);
    logLayout->addWidget(logTitle);
    logEdit_ = new QPlainTextEdit(logBlock);
    logEdit_->setObjectName(QStringLiteral("acquisitionLogEdit"));
    logEdit_->setReadOnly(true);
    logEdit_->setMinimumHeight(76);
    logEdit_->setMinimumWidth(360);
    logLayout->addWidget(logEdit_);
    diagnosticLayout->addWidget(logBlock, 1);

    auto *statusPanel = new QWidget(diagnosticFrame);
    auto *statusLayout = new QGridLayout(statusPanel);
    statusLayout->setContentsMargins(0, 0, 0, 0);
    statusLayout->setHorizontalSpacing(8);
    statusLayout->setVerticalSpacing(5);
    auto *statusTitle = new QLabel(QStringLiteral("设备与队列"), statusPanel);
    statusTitle->setProperty("sectionTitle", true);
    statusLayout->addWidget(statusTitle, 0, 0, 1, 2);

    roiTrackingStateLabel_ = new QLabel(tr("ROI 跟踪：未开始"), statusPanel);
    roiTrackingStateLabel_->setObjectName(
        QStringLiteral("roiTrackingStateLabel"));
    hardwareAoiStatusLabel_ = new QLabel(tr("AOI：全画幅"), statusPanel);
    hardwareAoiStatusLabel_->setObjectName(
        QStringLiteral("hardwareAoiStatusLabel"));
    droppedFramesLabel_ = new QLabel(tr("相机丢帧：0"), statusPanel);
    droppedFramesLabel_->setObjectName(QStringLiteral("droppedFramesLabel"));
    queueDroppedLabel_ = new QLabel(tr("队列丢帧：0"), statusPanel);
    queueDroppedLabel_->setObjectName(QStringLiteral("queueDroppedLabel"));
    lastErrorLabel_ = new QLabel(tr("最后错误：-"), statusPanel);
    lastErrorLabel_->setObjectName(QStringLiteral("lastErrorLabel"));

    for (QLabel *label : {roiTrackingStateLabel_, hardwareAoiStatusLabel_,
                          droppedFramesLabel_, queueDroppedLabel_,
                          lastErrorLabel_}) {
        label->setProperty("statusBadge", true);
        label->setWordWrap(true);
    }
    statusLayout->addWidget(roiTrackingStateLabel_, 1, 0);
    statusLayout->addWidget(hardwareAoiStatusLabel_, 1, 1);
    statusLayout->addWidget(droppedFramesLabel_, 2, 0);
    statusLayout->addWidget(queueDroppedLabel_, 2, 1);
    statusLayout->addWidget(lastErrorLabel_, 3, 0, 1, 2);
    diagnosticLayout->addWidget(statusPanel);
    rootLayout->addWidget(diagnosticFrame);
}
