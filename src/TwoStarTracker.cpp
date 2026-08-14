#include "TwoStarTracker.h"
#include "ConnectedDomain.h"
#include "StarSegmentation.h"

#include <opencv2/imgproc.hpp>

#include <QDateTime>

#include <algorithm>
#include <cmath>
#include <vector>

namespace {

struct FullFrameCandidate {
    QPointF position;
    double integratedIntensity = 0.0;
    int area = 0;
};

double distance(const QPointF &a, const QPointF &b)
{
    return std::hypot(a.x() - b.x(), a.y() - b.y());
}

// Otsu + configurable connected components over a full-frame Mono8 image. Returns the
// candidates filtered by the configured area range.
std::vector<FullFrameCandidate> extractFullFrameCandidates(
    const cv::Mat &mono8, const ProcessingConfig &config)
{
    std::vector<FullFrameCandidate> out;
    if (mono8.empty() || mono8.type() != CV_8UC1)
        return out;

    cv::Mat binary;
    const StarSegmentation::ForegroundSegmentation segmentation =
        StarSegmentation::segmentForegroundOtsu(
            mono8, config.otsuSigmaThreshold, config.otsuPeakFraction);
    if (!segmentation.valid)
        return out;
    binary = segmentation.mask;

    cv::Mat labels;
    cv::Mat stats;
    cv::Mat centroids;
    const int componentCount =
        cv::connectedComponentsWithStats(
            binary, labels, stats, centroids,
            ConnectedDomain::sanitizeConnectivity(config.connectivity), CV_32S);
    if (componentCount <= 1)
        return out;

    std::vector<double> integrated(componentCount, 0.0);
    for (int y = 0; y < labels.rows; ++y) {
        const int *labelRow = labels.ptr<int>(y);
        const std::uint8_t *valueRow = mono8.ptr<std::uint8_t>(y);
        for (int x = 0; x < labels.cols; ++x) {
            const int l = labelRow[x];
            if (l > 0)
                integrated[l] += static_cast<double>(valueRow[x]);
        }
    }

    for (int l = 1; l < componentCount; ++l) {
        const int area = stats.at<int>(l, cv::CC_STAT_AREA);
        if (area < config.otsuMinimumComponentAreaPx ||
            area > config.otsuMaximumComponentAreaPx)
            continue;
        FullFrameCandidate c;
        c.position = QPointF(centroids.at<double>(l, 0), centroids.at<double>(l, 1));
        c.integratedIntensity = integrated[l];
        c.area = area;
        out.push_back(c);
    }
    return out;
}

RoiRect makeRoiCentered(const QPointF &center, int width, int height,
                        int frameWidth, int frameHeight)
{
    int x = qRound(center.x()) - width / 2;
    int y = qRound(center.y()) - height / 2;
    x = qBound(0, x, std::max(0, frameWidth - width));
    y = qBound(0, y, std::max(0, frameHeight - height));
    return RoiRect{x, y, width, height};
}

} // namespace

TwoStarTracker::TwoStarTracker(AppConfig config)
    : config_(config)
    , centroidEngine_(config.processing)
    , hardwareAoi_{0, 0, 0, 0}
{
}

void TwoStarTracker::reset()
{
    hardwareAoi_ = RoiRect{0, 0, 0, 0};
    roiA_ = RoiRect{0, 0, 0, 0};
    roiB_ = RoiRect{0, 0, 0, 0};
    lastStarA_ = QPointF();
    lastStarB_ = QPointF();
    initialized_ = false;
    nearEdgeConsecutiveA_ = 0;
    nearEdgeConsecutiveB_ = 0;
    lostPairFrames_ = 0;
    lastAoiChangeMs_ = 0;
    aoiGeneration_ = 0;
}

bool TwoStarTracker::pairCandidates(const std::vector<QPointF> &candidates,
                                    QPointF &starA, QPointF &starB) const
{
    // Deterministic pairing: the two spots must be separated by at least
    // 2 * minimumPeakDistancePx, and among all valid pairs the one with the
    // largest separation is selected (ties broken by index order).
    const double minSeparation =
        config_.processing.minimumPeakDistancePx * 2.0;
    double bestSeparation = -1.0;
    std::size_t bestI = 0;
    std::size_t bestJ = 1;
    bool found = false;
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        for (std::size_t j = i + 1; j < candidates.size(); ++j) {
            const double sep = distance(candidates[i], candidates[j]);
            if (sep < minSeparation)
                continue;
            if (sep > bestSeparation) {
                bestSeparation = sep;
                bestI = i;
                bestJ = j;
                found = true;
            }
        }
    }
    if (!found)
        return false;
    starA = candidates[bestI];
    starB = candidates[bestJ];
    return true;
}

RoiRect TwoStarTracker::makeEnclosingAoi(QPointF starA, QPointF starB) const
{
    // One hardware AOI that contains both 64 x 64 software ROIs plus the
    // configured margin, clipped to the full-frame bounds.
    const int frameW = config_.acquisition.frameWidth;
    const int frameH = config_.acquisition.frameHeight;
    const int roiW = config_.processing.roiWidthPx;
    const int roiH = config_.processing.roiHeightPx;
    const int margin = config_.processing.hardwareAoiMarginPx;

    const RoiRect rA = makeRoiCentered(starA, roiW, roiH, frameW, frameH);
    const RoiRect rB = makeRoiCentered(starB, roiW, roiH, frameW, frameH);

    int x = std::min(rA.x, rB.x) - margin;
    int y = std::min(rA.y, rB.y) - margin;
    int right = std::max(rA.x + rA.width, rB.x + rB.width) + margin;
    int bottom = std::max(rA.y + rA.height, rB.y + rB.height) + margin;

    x = qBound(0, x, frameW - 1);
    y = qBound(0, y, frameH - 1);
    right = qBound(x + 1, right, frameW);
    bottom = qBound(y + 1, bottom, frameH);
    return RoiRect{x, y, right - x, bottom - y};
}

TrackerUpdate TwoStarTracker::locateFromFullFrame(const CameraFrame &frame)
{
    TrackerUpdate update;
    update.measurement.validPair = false;

    const std::vector<FullFrameCandidate> candidates =
        extractFullFrameCandidates(frame.mono8, config_.processing);
    if (candidates.empty()) {
        update.measurement.diagnostic = QStringLiteral("全画幅未找到候选目标");
        return update;
    }

    double maxIntensity = 0.0;
    double maxArea = 0.0;
    for (const auto &c : candidates) {
        maxIntensity = std::max(maxIntensity, c.integratedIntensity);
        maxArea = std::max(maxArea, static_cast<double>(c.area));
    }

    // Brightness and shape consistency: the two Polaris spots are comparable,
    // so discard much dimmer or much smaller components before pairing.
    std::vector<QPointF> positions;
    for (const auto &c : candidates) {
        if (c.integratedIntensity < 0.30 * maxIntensity)
            continue;
        if (static_cast<double>(c.area) < 0.30 * maxArea)
            continue;
        positions.push_back(c.position);
    }
    if (positions.size() < 2) {
        update.measurement.diagnostic = QStringLiteral("经过亮度/形状一致性过滤后，候选目标不足");
        return update;
    }

    QPointF starA;
    QPointF starB;
    if (!pairCandidates(positions, starA, starB)) {
        update.measurement.diagnostic = QStringLiteral("未找到满足最小间距要求的双星对");
        return update;
    }

    // Stable identities: left-to-right in image x on first initialization;
    // afterwards the candidate nearest the previous StarA keeps the StarA
    // identity. Identities never swap merely because the stars move.
    if (!initialized_) {
        if (starA.x() > starB.x())
            std::swap(starA, starB);
    } else if (distance(starA, lastStarA_) > distance(starB, lastStarA_)) {
        std::swap(starA, starB);
    }
    lastStarA_ = starA;
    lastStarB_ = starB;

    roiA_ = makeRoiCentered(starA, config_.processing.roiWidthPx,
                            config_.processing.roiHeightPx,
                            config_.acquisition.frameWidth,
                            config_.acquisition.frameHeight);
    roiB_ = makeRoiCentered(starB, config_.processing.roiWidthPx,
                            config_.processing.roiHeightPx,
                            config_.acquisition.frameWidth,
                            config_.acquisition.frameHeight);

    initialized_ = true;

    // Request one enclosing hardware AOI.
    update.requestHardwareAoi = true;
    update.requestedHardwareAoi = makeEnclosingAoi(starA, starB);
    update.requestGeneration = aoiGeneration_ + 1;

    update.measurement.validPair = true;
    update.measurement.diagnostic = QStringLiteral("全画幅定位成功");
    update.measurement.roiA = roiA_;
    update.measurement.roiB = roiB_;
    update.measurement.fullFrameStarA = starA;
    update.measurement.fullFrameStarB = starB;
    update.measurement.starA.valid = true;
    update.measurement.starA.centroidPx = starA;
    update.measurement.starB.valid = true;
    update.measurement.starB.centroidPx = starB;

    update.overlay.hasHardwareAoi = true;
    update.overlay.hasRois = true;
    update.overlay.hardwareAoi = update.requestedHardwareAoi;
    update.overlay.roiA = roiA_;
    update.overlay.roiB = roiB_;
    update.overlay.starA = starA;
    update.overlay.starB = starB;
    return update;
}

TrackerUpdate TwoStarTracker::processAoiFrame(const CameraFrame &frame)
{
    TrackerUpdate update;
    update.measurement.validPair = false;

    const int frameW = config_.acquisition.frameWidth;
    const int frameH = config_.acquisition.frameHeight;
    const int roiW = config_.processing.roiWidthPx;
    const int roiH = config_.processing.roiHeightPx;

    // Coordinate conversion: every software ROI is expressed in full-frame
    // coordinates. Reject the frame when a ROI is not contained in the current
    // AOI instead of cropping with wrong coordinates.
    for (const RoiRect *roi : {&roiA_, &roiB_}) {
        const int localX = roi->x - frame.sourceRect.x();
        const int localY = roi->y - frame.sourceRect.y();
        const cv::Rect localRect(localX, localY, roi->width, roi->height);
        const cv::Rect imageRect(0, 0, frame.mono8.cols, frame.mono8.rows);
        if ((localRect & imageRect) != localRect) {
            ++lostPairFrames_;
            update.measurement.diagnostic = QStringLiteral("软件 ROI 超出当前 AOI");
            if (lostPairFrames_ >= config_.processing.roiLostRelocalizationFrames) {
                update.requestFullFrameRelocalization = true;
                lostPairFrames_ = 0;
            }
            update.overlay = currentOverlay();
            return update;
        }
    }

    const cv::Rect localARect(roiA_.x - frame.sourceRect.x(),
                              roiA_.y - frame.sourceRect.y(), roiW, roiH);
    const cv::Rect localBRect(roiB_.x - frame.sourceRect.x(),
                              roiB_.y - frame.sourceRect.y(), roiW, roiH);
    const cv::Mat roiAImage(frame.mono8, localARect);
    const cv::Mat roiBImage(frame.mono8, localBRect);

    const QPointF expectedLocalA =
        lastStarA_ - QPointF(roiA_.x, roiA_.y);
    const QPointF expectedLocalB =
        lastStarB_ - QPointF(roiB_.x, roiB_.y);

    TwoStarMeasurement measurement;
    measurement.starA = centroidEngine_.locate(roiAImage, &expectedLocalA);
    measurement.starB = centroidEngine_.locate(roiBImage, &expectedLocalB);
    measurement.roiA = roiA_;
    measurement.roiB = roiB_;

    if (measurement.starA.valid && measurement.starB.valid) {
        measurement.fullFrameStarA =
            QPointF(roiA_.x + measurement.starA.centroidPx.x(),
                    roiA_.y + measurement.starA.centroidPx.y());
        measurement.fullFrameStarB =
            QPointF(roiB_.x + measurement.starB.centroidPx.x(),
                    roiB_.y + measurement.starB.centroidPx.y());
        measurement.validPair = true;
        measurement.diagnostic = QStringLiteral("有效双星");
        lastStarA_ = measurement.fullFrameStarA;
        lastStarB_ = measurement.fullFrameStarB;
        lostPairFrames_ = 0;
    } else {
        measurement.diagnostic =
            measurement.starA.valid ? QStringLiteral("StarB 丢失") : QStringLiteral("StarA 丢失");
        ++lostPairFrames_;
        if (lostPairFrames_ >= config_.processing.roiLostRelocalizationFrames) {
            update.requestFullFrameRelocalization = true;
            lostPairFrames_ = 0;
        }
    }

    update.measurement = measurement;
    update.overlay = currentOverlay();
    updateRecenteringState(measurement, update);
    return update;
}

void TwoStarTracker::applyHardwareAoi(RoiRect aoi, std::uint64_t generation)
{
    hardwareAoi_ = aoi;
    aoiGeneration_ = generation;
}

void TwoStarTracker::updateRecenteringState(const TwoStarMeasurement &measurement,
                                            TrackerUpdate &update)
{
    if (!measurement.validPair)
        return;

    const ProcessingConfig &p = config_.processing;

    const auto starNearEdge = [&](const CentroidMeasurement &star,
                                  const RoiRect &roi) {
        return star.centroidPx.x() < p.roiRecenteringDistanceToEdgePx ||
               star.centroidPx.x() > (roi.width - p.roiRecenteringDistanceToEdgePx) ||
               star.centroidPx.y() < p.roiRecenteringDistanceToEdgePx ||
               star.centroidPx.y() > (roi.height - p.roiRecenteringDistanceToEdgePx);
    };

    const bool aNearEdge = starNearEdge(measurement.starA, roiA_);
    const bool bNearEdge = starNearEdge(measurement.starB, roiB_);
    nearEdgeConsecutiveA_ = aNearEdge ? nearEdgeConsecutiveA_ + 1 : 0;
    nearEdgeConsecutiveB_ = bNearEdge ? nearEdgeConsecutiveB_ + 1 : 0;

    if (nearEdgeConsecutiveA_ < p.roiRecenteringConsecutiveFrames &&
        nearEdgeConsecutiveB_ < p.roiRecenteringConsecutiveFrames)
        return;

    nearEdgeConsecutiveA_ = 0;
    nearEdgeConsecutiveB_ = 0;

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (nowMs - lastAoiChangeMs_ < p.roiRecenteringCooldownMs)
        return;

    const int frameW = config_.acquisition.frameWidth;
    const int frameH = config_.acquisition.frameHeight;

    RoiRect newA = makeRoiCentered(measurement.fullFrameStarA, p.roiWidthPx,
                                   p.roiHeightPx, frameW, frameH);
    RoiRect newB = makeRoiCentered(measurement.fullFrameStarB, p.roiWidthPx,
                                   p.roiHeightPx, frameW, frameH);

    const double shiftA =
        distance(QPointF(newA.x + newA.width / 2.0, newA.y + newA.height / 2.0),
                 QPointF(roiA_.x + roiA_.width / 2.0, roiA_.y + roiA_.height / 2.0));
    const double shiftB =
        distance(QPointF(newB.x + newB.width / 2.0, newB.y + newB.height / 2.0),
                 QPointF(roiB_.x + roiB_.width / 2.0, roiB_.y + roiB_.height / 2.0));

    if (std::max(shiftA, shiftB) < p.roiRecenteringMinimumShiftPx)
        return;

    roiA_ = newA;
    roiB_ = newB;
    lastAoiChangeMs_ = nowMs;

    update.requestHardwareAoi = true;
    update.requestedHardwareAoi =
        makeEnclosingAoi(measurement.fullFrameStarA, measurement.fullFrameStarB);
    update.requestGeneration = aoiGeneration_ + 1;
}

RoiOverlay TwoStarTracker::currentOverlay() const
{
    RoiOverlay overlay;
    overlay.hardwareAoi = hardwareAoi_;
    overlay.roiA = roiA_;
    overlay.roiB = roiB_;
    overlay.starA = lastStarA_;
    overlay.starB = lastStarB_;
    overlay.hasHardwareAoi = hardwareAoi_.isValid();
    overlay.hasRois = initialized_;
    return overlay;
}

RoiRect TwoStarTracker::hardwareAoi() const
{
    return hardwareAoi_;
}

RoiRect TwoStarTracker::roiA() const
{
    return roiA_;
}

RoiRect TwoStarTracker::roiB() const
{
    return roiB_;
}

bool TwoStarTracker::initialized() const
{
    return initialized_;
}
