#pragma once

#include "CameraTypes.h"
#include "ProcessingTypes.h"

#include <QObject>

#include <functional>
#include <memory>

// This is the ONLY module allowed to include pylon headers and use pylon node
// maps. The remainder of the program sees only CameraFrame, CameraCapabilities,
// and CameraStatistics.
class PylonCamera final {
public:
    PylonCamera();
    ~PylonCamera();

    bool openFirstCompatibleCamera(QString *error);
    void close();
    bool isOpen() const;

    CameraCapabilities capabilities() const;
    CameraStatistics statistics() const;

    bool configureMono8(QString *error);
    bool configureExposure(double exposureTimeMs, QString *error);
    bool configureAcquisitionRate(double frameRateHz, QString *error);
    bool configureTrigger(const TriggerConfig &config, QString *error);
    bool configureHardwareAoi(const RoiRect &aoi, QString *error);
    bool resetToFullFrame(QString *error);

    bool startGrabbing(QString *error);
    void stopGrabbing();
    bool executeSoftwareTrigger(QString *error);

    // The callback must only copy a Mono8 buffer and metadata into CameraFrame.
    void setFrameCallback(std::function<void(CameraFrame)> callback);

    // Generation of the active hardware AOI. Incremented whenever the AOI is
    // reconfigured so frames from an old AOI can be identified and discarded.
    std::uint64_t configurationGeneration() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
