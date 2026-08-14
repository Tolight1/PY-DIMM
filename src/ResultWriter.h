#pragma once

#include "AppConfig.h"
#include "CameraTypes.h"
#include "ProcessingTypes.h"

#include <QFile>
#include <QString>

class ResultWriter final {
public:
    ResultWriter() = default;
    ~ResultWriter();

    bool startRun(const AppConfig &config,
                  const CameraCapabilities &capabilities,
                  QString *error);
    void append(const MeasurementResult &result);
    void appendCameraStats(const CameraStatistics &stats);
    void finishRun();
    bool isRunning() const;
    QString runDirectory() const;

private:
    QString runDirectory_;
    QFile atmosphereFile_;
    QFile centroidFile_;
    QFile diagnosticsFile_;
    bool running_ = false;
    double resultRecordIntervalSec_ = 1.0;
    qint64 lastResultRecordMs_ = -1;
};
