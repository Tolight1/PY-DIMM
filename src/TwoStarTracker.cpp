#include "TwoStarTracker.h"
#include "ConnectedDomain.h"
#include "StarSegmentation.h"

#include <opencv2/imgproc.hpp>

#include <QDateTime>

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace {

struct StarCandidate {
    QPointF localPosition;
    double integratedIntensity = 0.0;
    int area = 0;
};

double distance(const QPointF &a, const QPointF &b)
{
    return std::hypot(a.x() - b.x(), a.y() - b.y());
}

double median(std::vector<double> values)
{
    if (values.empty())
        return 0.0;
    std::sort(values.begin(), values.end());
    const std::size_t middle = values.size() / 2;
    if (values.size() % 2 == 1)
        return values[middle];
    return 0.5 * (values[middle - 1] + values[middle]);
}

QPointF componentWiseMedian(const std::deque<QPointF> &history)
{
    std::vector<double> x;
    std::vector<double> y;
    x.reserve(history.size());
    y.reserve(history.size());
    for (const QPointF &value : history) {
        x.push_back(value.x());
        y.push_back(value.y());
    }
    return QPointF(median(std::move(x)), median(std::move(y)));
}

constexpr std::size_t kDifferentialBaselineMinimumSamples = 5;

std::vector<StarCandidate> extractCandidates(const cv::Mat &mono8,
                                              const ProcessingConfig &config)
{
    std::vector<StarCandidate> candidates;
    if (mono8.empty() || mono8.type() != CV_8UC1)
        return candidates;

    const StarSegmentation::ForegroundSegmentation segmentation =
        StarSegmentation::segmentForegroundOtsu(
            mono8, config.otsuSigmaThreshold, config.otsuPeakFraction);
    if (!segmentation.valid)
        return candidates;

    cv::Mat labels;
    cv::Mat stats;
    cv::Mat centroids;
    const int componentCount = cv::connectedComponentsWithStats(
        segmentation.mask, labels, stats, centroids,
        ConnectedDomain::sanitizeConnectivity(config.connectivity), CV_32S);
    if (componentCount <= 1)
        return candidates;

    std::vector<double> integrated(componentCount, 0.0);
    for (int y = 0; y < labels.rows; ++y) {
        const int *labelRow = labels.ptr<int>(y);
        const std::uint8_t *valueRow = mono8.ptr<std::uint8_t>(y);
        for (int x = 0; x < labels.cols; ++x) {
            const int label = labelRow[x];
            if (label > 0)
                integrated[label] += static_cast<double>(valueRow[x]);
        }
    }

    for (int label = 1; label < componentCount; ++label) {
        const int area = stats.at<int>(label, cv::CC_STAT_AREA);
        if (area < config.otsuMinimumComponentAreaPx ||
            area > config.otsuMaximumComponentAreaPx)
            continue;
        candidates.push_back(StarCandidate{
            QPointF(centroids.at<double>(label, 0),
                    centroids.at<double>(label, 1)),
            integrated[label],
            area});
    }

    // The two largest valid connected components are the only pair candidates.
    // Brightness is a deterministic tie-breaker, never a relative rejection.
    std::sort(candidates.begin(), candidates.end(),
              [](const StarCandidate &lhs, const StarCandidate &rhs) {
                  if (lhs.area != rhs.area)
                      return lhs.area > rhs.area;
                  if (lhs.integratedIntensity != rhs.integratedIntensity)
                      return lhs.integratedIntensity > rhs.integratedIntensity;
                  if (lhs.localPosition.x() != rhs.localPosition.x())
                      return lhs.localPosition.x() < rhs.localPosition.x();
                  return lhs.localPosition.y() < rhs.localPosition.y();
              });
    return candidates;
}

RoiRect makeCenteredWindow(const QPointF &center, int size,
                           int frameWidth, int frameHeight)
{
    int x = qRound(center.x()) - size / 2;
    int y = qRound(center.y()) - size / 2;
    x = qBound(0, x, std::max(0, frameWidth - size));
    y = qBound(0, y, std::max(0, frameHeight - size));
    return RoiRect{x, y, std::min(size, frameWidth),
                   std::min(size, frameHeight)};
}

QPointF toFullFrame(const QPointF &local, const QRect &sourceRect)
{
    return QPointF(local.x() + sourceRect.x(),
                   local.y() + sourceRect.y());
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
    lastStarA_ = QPointF();
    lastStarB_ = QPointF();
    initialized_ = false;
    centroidsValid_ = false;
    lostPairFrames_ = 0;
    differentialBaselineHistory_.clear();
    differentialBaselineViolationFrames_ = 0;
    lastHardwareAoiRequestMs_ = 0;
    aoiGeneration_ = 0;
}

bool TwoStarTracker::needsHardwareAoiUpdate(const QPointF &starA,
                                             const QPointF &starB) const
{
    if (!hardwareAoi_.isValid())
        return true;

    const int frameWidth = config_.acquisition.frameWidth;
    const int frameHeight = config_.acquisition.frameHeight;
    const int kernelSize = 2 * config_.processing.centroidKernelRadiusPx + 1;
    const int safety = config_.processing.hardwareAoiUpdateDistanceToEdgePx;
    const auto safelyContained = [&](const QPointF &star) {
        const RoiRect window = makeCenteredWindow(
            star, kernelSize, frameWidth, frameHeight);
        return window.x >= hardwareAoi_.x + safety &&
               window.y >= hardwareAoi_.y + safety &&
               window.x + window.width <=
                   hardwareAoi_.x + hardwareAoi_.width - safety &&
               window.y + window.height <=
                   hardwareAoi_.y + hardwareAoi_.height - safety;
    };
    return !safelyContained(starA) || !safelyContained(starB);
}

bool TwoStarTracker::hasContinuousDifferential(const QPointF &starA,
                                                const QPointF &starB,
                                                double *jumpPx) const
{
    if (jumpPx)
        *jumpPx = 0.0;
    if (!initialized_)
        return true;

    const QPointF previousDifferential = lastStarB_ - lastStarA_;
    const QPointF currentDifferential = starB - starA;
    const double jump = distance(currentDifferential,
                                 previousDifferential);
    if (jumpPx)
        *jumpPx = jump;
    return jump <= config_.processing.maximumDifferentialJumpPx;
}

double TwoStarTracker::differentialBaselineDeviation(
    const QPointF &starA, const QPointF &starB) const
{
    if (differentialBaselineHistory_.size() <
        kDifferentialBaselineMinimumSamples)
        return 0.0;

    const QPointF baseline = componentWiseMedian(differentialBaselineHistory_);
    return distance(starB - starA, baseline);
}

void TwoStarTracker::rememberDifferential(const QPointF &starA,
                                          const QPointF &starB)
{
    differentialBaselineHistory_.push_back(starB - starA);
    while (differentialBaselineHistory_.size() >
           static_cast<std::size_t>(kDifferentialBaselineWindowFrames))
        differentialBaselineHistory_.pop_front();
}

void TwoStarTracker::seedDifferentialBaseline(const QPointF &starA,
                                               const QPointF &starB)
{
    differentialBaselineHistory_.clear();
    differentialBaselineViolationFrames_ = 0;
    rememberDifferential(starA, starB);
}

RoiRect TwoStarTracker::makeEnclosingAoi(QPointF starA, QPointF starB) const
{
    const int frameWidth = config_.acquisition.frameWidth;
    const int frameHeight = config_.acquisition.frameHeight;
    const int kernelSize = 2 * config_.processing.centroidKernelRadiusPx + 1;
    const int margin = config_.processing.hardwareAoiMarginPx;

    const RoiRect windowA = makeCenteredWindow(
        starA, kernelSize, frameWidth, frameHeight);
    const RoiRect windowB = makeCenteredWindow(
        starB, kernelSize, frameWidth, frameHeight);

    int x = std::min(windowA.x, windowB.x) - margin;
    int y = std::min(windowA.y, windowB.y) - margin;
    int right = std::max(windowA.x + windowA.width,
                         windowB.x + windowB.width) + margin;
    int bottom = std::max(windowA.y + windowA.height,
                          windowB.y + windowB.height) + margin;

    x = qBound(0, x, std::max(0, frameWidth - 1));
    y = qBound(0, y, std::max(0, frameHeight - 1));
    right = qBound(x + 1, right, frameWidth);
    bottom = qBound(y + 1, bottom, frameHeight);
    return RoiRect{x, y, right - x, bottom - y};
}

TrackerUpdate TwoStarTracker::locateFromFullFrame(const CameraFrame &frame)
{
    TrackerUpdate update;
    const std::vector<StarCandidate> candidates =
        extractCandidates(frame.mono8, config_.processing);
    if (candidates.size() < 2) {
        centroidsValid_ = false;
        update.measurement.diagnostic =
            QStringLiteral("全画幅候选连通域不足两个（当前 %1 个）")
                .arg(static_cast<int>(candidates.size()));
        update.overlay = currentOverlay();
        return update;
    }

    StarCandidate candidateA = candidates[0];
    StarCandidate candidateB = candidates[1];
    const QPointF firstA = toFullFrame(candidateA.localPosition,
                                        frame.sourceRect);
    const QPointF firstB = toFullFrame(candidateB.localPosition,
                                        frame.sourceRect);
    if (distance(firstA, firstB) <
        config_.processing.minimumPeakDistancePx * 2.0) {
        centroidsValid_ = false;
        update.measurement.diagnostic =
            QStringLiteral("未找到满足最小间距要求的双星对");
        update.overlay = currentOverlay();
        return update;
    }

    QPointF coarseA = candidateA.localPosition;
    QPointF coarseB = candidateB.localPosition;
    QPointF previousA = toFullFrame(coarseA, frame.sourceRect);
    QPointF previousB = toFullFrame(coarseB, frame.sourceRect);
    if (initialized_) {
        if (distance(previousA, lastStarA_) > distance(previousB, lastStarA_)) {
            std::swap(candidateA, candidateB);
            coarseA = candidateA.localPosition;
            coarseB = candidateB.localPosition;
        }
    } else if (previousA.x() > previousB.x()) {
        std::swap(candidateA, candidateB);
        coarseA = candidateA.localPosition;
        coarseB = candidateB.localPosition;
    }

    const CentroidMeasurement measuredA =
        centroidEngine_.refineInKernel(frame.mono8, coarseA);
    const CentroidMeasurement measuredB =
        centroidEngine_.refineInKernel(frame.mono8, coarseB);
    if (!measuredA.valid || !measuredB.valid) {
        centroidsValid_ = false;
        update.measurement.starA = measuredA;
        update.measurement.starB = measuredB;
        update.measurement.diagnostic = QStringLiteral("全画幅质心窗口无效");
        update.overlay = currentOverlay();
        return update;
    }

    const QPointF starA = toFullFrame(measuredA.centroidPx, frame.sourceRect);
    const QPointF starB = toFullFrame(measuredB.centroidPx, frame.sourceRect);
    lastStarA_ = starA;
    lastStarB_ = starB;
    initialized_ = true;
    centroidsValid_ = true;
    seedDifferentialBaseline(starA, starB);

    update.measurement.starA = measuredA;
    update.measurement.starB = measuredB;
    update.measurement.fullFrameStarA = starA;
    update.measurement.fullFrameStarB = starB;
    update.measurement.validPair = true;

    // The first AOI request after startup or full-frame relocalization is
    // immediate, but it still starts the same cooldown as a tracking request.
    // Otherwise the first AOI frame can trigger a second hardware reconfigure
    // before the configured cooldown has elapsed.
    lastHardwareAoiRequestMs_ = QDateTime::currentMSecsSinceEpoch();
    update.requestHardwareAoi = true;
    update.requestedHardwareAoi = makeEnclosingAoi(starA, starB);
    update.requestGeneration = aoiGeneration_ + 1;
    if (update.requestedHardwareAoi.width >
            config_.processing.hardwareAoiMaxWidthPx ||
        update.requestedHardwareAoi.height >
            config_.processing.hardwareAoiMaxHeightPx) {
        update.measurement.diagnostic =
            QStringLiteral("全画幅定位成功；AOI %1 × %2 px 超出性能目标 %3 × %4 px")
                .arg(update.requestedHardwareAoi.width)
                .arg(update.requestedHardwareAoi.height)
                .arg(config_.processing.hardwareAoiMaxWidthPx)
                .arg(config_.processing.hardwareAoiMaxHeightPx);
    } else {
        update.measurement.diagnostic =
            QStringLiteral("全画幅定位成功，已选择面积最大的两个有效连通域");
    }

    update.overlay.hardwareAoi = update.requestedHardwareAoi;
    update.overlay.hasHardwareAoi = config_.acquisition.enableHardwareAoi;
    update.overlay.starA = starA;
    update.overlay.starB = starB;
    update.overlay.hasCentroids = true;
    return update;
}

TrackerUpdate TwoStarTracker::processAoiFrame(const CameraFrame &frame)
{
    TrackerUpdate update;
    const std::vector<StarCandidate> candidates =
        extractCandidates(frame.mono8, config_.processing);
    if (candidates.size() < 2) {
        centroidsValid_ = false;
        ++lostPairFrames_;
        update.measurement.diagnostic =
            QStringLiteral("AOI 内候选连通域不足两个（当前 %1 个）")
                .arg(static_cast<int>(candidates.size()));
        if (lostPairFrames_ >=
            config_.processing.lostPairRelocalizationFrames) {
            update.requestFullFrameRelocalization = true;
            lostPairFrames_ = 0;
        }
        update.overlay = currentOverlay();
        return update;
    }

    StarCandidate candidateA = candidates[0];
    StarCandidate candidateB = candidates[1];
    QPointF starA = toFullFrame(candidateA.localPosition, frame.sourceRect);
    QPointF starB = toFullFrame(candidateB.localPosition, frame.sourceRect);
    if (distance(starA, starB) <
        config_.processing.minimumPeakDistancePx * 2.0) {
        centroidsValid_ = false;
        ++lostPairFrames_;
        update.measurement.diagnostic =
            QStringLiteral("AOI 内双星候选未达到最小间距");
        if (lostPairFrames_ >=
            config_.processing.lostPairRelocalizationFrames) {
            update.requestFullFrameRelocalization = true;
            lostPairFrames_ = 0;
        }
        update.overlay = currentOverlay();
        return update;
    }

    if (initialized_ && distance(starA, lastStarA_) >
                            distance(starB, lastStarA_)) {
        std::swap(candidateA, candidateB);
        starA = toFullFrame(candidateA.localPosition, frame.sourceRect);
        starB = toFullFrame(candidateB.localPosition, frame.sourceRect);
    }

    const CentroidMeasurement measuredA = centroidEngine_.refineInKernel(
        frame.mono8, candidateA.localPosition);
    const CentroidMeasurement measuredB = centroidEngine_.refineInKernel(
        frame.mono8, candidateB.localPosition);
    if (!measuredA.valid || !measuredB.valid) {
        centroidsValid_ = false;
        ++lostPairFrames_;
        update.measurement.starA = measuredA;
        update.measurement.starB = measuredB;
        update.measurement.diagnostic = QStringLiteral("AOI 内质心窗口无效");
        if (lostPairFrames_ >=
            config_.processing.lostPairRelocalizationFrames) {
            update.requestFullFrameRelocalization = true;
            lostPairFrames_ = 0;
        }
        update.overlay = currentOverlay();
        return update;
    }

    starA = toFullFrame(measuredA.centroidPx, frame.sourceRect);
    starB = toFullFrame(measuredB.centroidPx, frame.sourceRect);

    double differentialJumpPx = 0.0;
    if (!hasContinuousDifferential(starA, starB, &differentialJumpPx)) {
        // Keep the last accepted star pair as the tracking reference. A
        // wrong connected-component association must not enter the rolling
        // DIMM window or move the AOI based on a single bad frame.
        ++lostPairFrames_;
        update.measurement.starA = measuredA;
        update.measurement.starB = measuredB;
        update.measurement.fullFrameStarA = starA;
        update.measurement.fullFrameStarB = starB;
        update.measurement.diagnostic =
            QStringLiteral("双星差分跳变 %1 px，已拒绝该样本")
                .arg(differentialJumpPx, 0, 'f', 3);
        update.differentialContinuityRejected = true;
        update.differentialJumpPx = differentialJumpPx;
        update.differentialBaselineDeviationPx =
            differentialBaselineDeviation(starA, starB);
        update.differentialBaselineViolationFrames =
            differentialBaselineViolationFrames_;
        if (lostPairFrames_ >=
            config_.processing.lostPairRelocalizationFrames) {
            update.requestFullFrameRelocalization = true;
            lostPairFrames_ = 0;
        }
        update.overlay = currentOverlay();
        return update;
    }

    const double baselineDeviation =
        differentialBaselineDeviation(starA, starB);
    const bool baselineReady = differentialBaselineHistory_.size() >=
                               kDifferentialBaselineMinimumSamples;
    if (baselineReady &&
        baselineDeviation > config_.processing.maximumDifferentialJumpPx) {
        ++differentialBaselineViolationFrames_;
    } else {
        differentialBaselineViolationFrames_ = 0;
    }

    update.differentialJumpPx = differentialJumpPx;
    update.differentialBaselineDeviationPx = baselineDeviation;
    update.differentialBaselineViolationFrames =
        differentialBaselineViolationFrames_;
    if (baselineReady &&
        differentialBaselineViolationFrames_ >=
            config_.processing.differentialBaselineViolationFrames) {
        // Keep the last accepted pair as the tracking reference. The rolling
        // measurement window remains intact; only this invalid pair is
        // isolated after the configured persistent-baseline threshold.
        ++lostPairFrames_;
        update.measurement.starA = measuredA;
        update.measurement.starB = measuredB;
        update.measurement.fullFrameStarA = starA;
        update.measurement.fullFrameStarB = starB;
        update.measurement.diagnostic =
            QStringLiteral("双星差分偏离短窗口基线 %1 px，连续 %2/%3 帧，已拒绝该样本")
                .arg(baselineDeviation, 0, 'f', 3)
                .arg(differentialBaselineViolationFrames_)
                .arg(config_.processing.differentialBaselineViolationFrames);
        update.differentialContinuityRejected = true;
        if (lostPairFrames_ >=
            config_.processing.lostPairRelocalizationFrames) {
            update.requestFullFrameRelocalization = true;
            lostPairFrames_ = 0;
        }
        update.overlay = currentOverlay();
        return update;
    }

    lastStarA_ = starA;
    lastStarB_ = starB;
    centroidsValid_ = true;
    lostPairFrames_ = 0;
    if (!baselineReady ||
        baselineDeviation <= config_.processing.maximumDifferentialJumpPx)
        rememberDifferential(starA, starB);

    update.measurement.starA = measuredA;
    update.measurement.starB = measuredB;
    update.measurement.fullFrameStarA = starA;
    update.measurement.fullFrameStarB = starB;
    update.measurement.validPair = true;
    update.measurement.diagnostic = baselineReady &&
            differentialBaselineViolationFrames_ > 0
        ? QStringLiteral("有效双星（差分基线偏离 %1 px，连续 %2/%3 帧）")
              .arg(baselineDeviation, 0, 'f', 3)
              .arg(differentialBaselineViolationFrames_)
              .arg(config_.processing.differentialBaselineViolationFrames)
        : QStringLiteral("有效双星");

    if (config_.acquisition.enableHardwareAoi &&
        needsHardwareAoiUpdate(starA, starB)) {
        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
        const ProcessingConfig &p = config_.processing;
        if (nowMs - lastHardwareAoiRequestMs_ >=
            p.hardwareAoiUpdateCooldownMs) {
            const RoiRect requestedAoi = makeEnclosingAoi(starA, starB);
            if (hardwareAoi_.isValid()) {
                const int shiftX = std::abs(requestedAoi.x - hardwareAoi_.x);
                const int shiftY = std::abs(requestedAoi.y - hardwareAoi_.y);
                const int shiftW = std::abs(requestedAoi.width - hardwareAoi_.width);
                const int shiftH = std::abs(requestedAoi.height - hardwareAoi_.height);
                const int maxShift = std::max(
                    std::max(shiftX, shiftY), std::max(shiftW, shiftH));
                if (static_cast<double>(maxShift) >=
                    p.hardwareAoiUpdateMinimumShiftPx) {
                    lastHardwareAoiRequestMs_ = nowMs;
                    update.requestHardwareAoi = true;
                    update.requestedHardwareAoi = requestedAoi;
                    update.requestGeneration = aoiGeneration_ + 1;
                }
            } else {
                lastHardwareAoiRequestMs_ = nowMs;
                update.requestHardwareAoi = true;
                update.requestedHardwareAoi = requestedAoi;
                update.requestGeneration = aoiGeneration_ + 1;
            }
        }
    }

    update.overlay = currentOverlay();
    return update;
}

void TwoStarTracker::applyHardwareAoi(RoiRect aoi, std::uint64_t generation)
{
    hardwareAoi_ = aoi;
    aoiGeneration_ = generation;
    // Hide the previous-frame crosses until the first valid pair in the new
    // camera coordinate frame is measured.
    centroidsValid_ = false;
    // The differential vector is expressed in full-frame coordinates and is
    // invariant to an ordinary hardware-AOI crop. Keep its baseline across
    // AOI moves so a common translation does not start a new segment.
}

DisplayOverlay TwoStarTracker::currentOverlay() const
{
    DisplayOverlay overlay;
    overlay.hardwareAoi = hardwareAoi_;
    overlay.starA = lastStarA_;
    overlay.starB = lastStarB_;
    overlay.hasHardwareAoi = hardwareAoi_.isValid();
    overlay.hasCentroids = centroidsValid_;
    return overlay;
}

RoiRect TwoStarTracker::hardwareAoi() const
{
    return hardwareAoi_;
}

bool TwoStarTracker::initialized() const
{
    return initialized_;
}
