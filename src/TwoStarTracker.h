#pragma once

#include "AppConfig.h"
#include "CentroidEngine.h"

#include <QObject>

#include <vector>

struct TrackerUpdate {
    TwoStarMeasurement measurement;
    RoiOverlay overlay;
    bool requestHardwareAoi = false;
    RoiRect requestedHardwareAoi;
    std::uint64_t requestGeneration = 0;
    bool requestFullFrameRelocalization = false;
};

// Full-frame locating, StarA/StarB identity, ROI tracking, and hardware AOI
// requests. Pure logic class: no pylon and no Qt widgets.
class TwoStarTracker final {
public:
    explicit TwoStarTracker(AppConfig config);

    void reset();
    TrackerUpdate locateFromFullFrame(const CameraFrame &frame);
    TrackerUpdate processAoiFrame(const CameraFrame &frame);
    void applyHardwareAoi(RoiRect aoi, std::uint64_t generation);

    RoiRect hardwareAoi() const;
    RoiRect roiA() const;
    RoiRect roiB() const;
    bool initialized() const;

private:
    bool pairCandidates(const std::vector<QPointF> &candidates,
                        QPointF &starA, QPointF &starB) const;
    RoiRect makeEnclosingAoi(QPointF starA, QPointF starB) const;
    void updateRecenteringState(const TwoStarMeasurement &measurement,
                                TrackerUpdate &update);
    RoiOverlay currentOverlay() const;

    AppConfig config_;
    CentroidEngine centroidEngine_;
    RoiRect hardwareAoi_;
    RoiRect roiA_;
    RoiRect roiB_;
    QPointF lastStarA_;
    QPointF lastStarB_;
    bool initialized_ = false;
    int nearEdgeConsecutiveA_ = 0;
    int nearEdgeConsecutiveB_ = 0;
    int lostPairFrames_ = 0;
    qint64 lastAoiChangeMs_ = 0;
    std::uint64_t aoiGeneration_ = 0;
};
