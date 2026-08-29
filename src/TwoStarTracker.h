#pragma once

#include "AppConfig.h"
#include "CentroidEngine.h"

#include <QObject>

#include <deque>
#include <vector>

struct TrackerUpdate {
    TwoStarMeasurement measurement;
    DisplayOverlay overlay;
    bool differentialContinuityRejected = false;
    double differentialJumpPx = 0.0;
    double differentialBaselineDeviationPx = 0.0;
    int differentialBaselineViolationFrames = 0;
    bool requestHardwareAoi = false;
    RoiRect requestedHardwareAoi;
    std::uint64_t requestGeneration = 0;
    bool requestFullFrameRelocalization = false;
};

// Full-frame locating, StarA/StarB identity, centroid tracking, and hardware AOI
// requests. Pure logic class: no pylon and no Qt widgets.
class TwoStarTracker final {
public:
    explicit TwoStarTracker(AppConfig config);

    void reset();
    TrackerUpdate locateFromFullFrame(const CameraFrame &frame);
    TrackerUpdate processAoiFrame(const CameraFrame &frame);
    void applyHardwareAoi(RoiRect aoi, std::uint64_t generation);

    RoiRect hardwareAoi() const;
    DisplayOverlay currentOverlay() const;
    bool initialized() const;

private:
    bool needsHardwareAoiUpdate(const QPointF &starA,
                                const QPointF &starB) const;
    bool hasContinuousDifferential(const QPointF &starA,
                                   const QPointF &starB,
                                   double *jumpPx) const;
    double differentialBaselineDeviation(const QPointF &starA,
                                         const QPointF &starB) const;
    void rememberDifferential(const QPointF &starA, const QPointF &starB);
    void seedDifferentialBaseline(const QPointF &starA, const QPointF &starB);
    RoiRect makeEnclosingAoi(QPointF starA, QPointF starB) const;
    AppConfig config_;
    CentroidEngine centroidEngine_;
    RoiRect hardwareAoi_;
    QPointF lastStarA_;
    QPointF lastStarB_;
    bool initialized_ = false;
    bool centroidsValid_ = false;
    int lostPairFrames_ = 0;
    std::deque<QPointF> differentialBaselineHistory_;
    int differentialBaselineViolationFrames_ = 0;
    qint64 lastHardwareAoiRequestMs_ = 0;
    std::uint64_t aoiGeneration_ = 0;
};
