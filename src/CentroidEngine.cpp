#include "CentroidEngine.h"
#include "ConnectedDomain.h"
#include "StarSegmentation.h"

#include <opencv2/imgproc.hpp>

#include <QtGlobal>

#include <algorithm>
#include <cmath>
#include <vector>

namespace {
struct ComponentCandidate {
    int label = 0;
    double centroidX = 0.0;
    double centroidY = 0.0;
    int area = 0;
    double integratedIntensity = 0.0;
    double peakIntensity = 0.0;
};
} // namespace

CentroidEngine::CentroidEngine(ProcessingConfig config)
    : config_(config)
{
}

void CentroidEngine::setConfig(ProcessingConfig config)
{
    config_ = config;
}

CentroidMeasurement CentroidEngine::locate(const cv::Mat &roiMono8,
                                           const QPointF *expectedLocalCentroid) const
{
    CentroidMeasurement result;
    CV_Assert(roiMono8.type() == CV_8UC1);
    if (roiMono8.empty()) {
        result.valid = false;
        result.diagnostic = QStringLiteral("ROI 为空");
        return result;
    }

    cv::Mat binary;
    const StarSegmentation::ForegroundSegmentation segmentation =
        StarSegmentation::segmentForegroundOtsu(
            roiMono8, config_.otsuSigmaThreshold, config_.otsuPeakFraction);
    if (!segmentation.valid) {
        result.valid = false;
        result.diagnostic = QStringLiteral("Otsu 鍒嗗壊澶辫触");
        return result;
    }
    binary = segmentation.mask;

    cv::Mat labels;
    cv::Mat stats;
    cv::Mat centroids;
    const int componentCount = cv::connectedComponentsWithStats(
        binary, labels, stats, centroids,
        ConnectedDomain::sanitizeConnectivity(config_.connectivity), CV_32S);

    // Per-component integrated/peak intensity from the ORIGINAL Mono8 image.
    std::vector<double> integrated(componentCount, 0.0);
    std::vector<double> peak(componentCount, 0.0);
    for (int y = 0; y < labels.rows; ++y) {
        const int *labelRow = labels.ptr<int>(y);
        const std::uint8_t *valueRow = roiMono8.ptr<std::uint8_t>(y);
        for (int x = 0; x < labels.cols; ++x) {
            const int l = labelRow[x];
            if (l <= 0)
                continue;
            const double v = static_cast<double>(valueRow[x]);
            integrated[l] += v;
            peak[l] = std::max(peak[l], v);
        }
    }

    std::vector<ComponentCandidate> candidates;
    for (int l = 1; l < componentCount; ++l) {
        const int area = stats.at<int>(l, cv::CC_STAT_AREA);
        if (area < config_.otsuMinimumComponentAreaPx ||
            area > config_.otsuMaximumComponentAreaPx)
            continue;
        ComponentCandidate c;
        c.label = l;
        c.centroidX = centroids.at<double>(l, 0);
        c.centroidY = centroids.at<double>(l, 1);
        c.area = area;
        c.integratedIntensity = integrated[l];
        c.peakIntensity = peak[l];
        candidates.push_back(c);
    }

    if (candidates.empty()) {
        result.valid = false;
        result.diagnostic = QStringLiteral("未找到连通域");
        return result;
    }

    const ComponentCandidate *best = nullptr;
    if (expectedLocalCentroid) {
        // Score candidates by distance to the expected location, then by
        // integrated intensity. Deterministic.
        double bestDistance = std::numeric_limits<double>::max();
        for (const auto &c : candidates) {
            const double dx = c.centroidX - expectedLocalCentroid->x();
            const double dy = c.centroidY - expectedLocalCentroid->y();
            const double distance = std::sqrt(dx * dx + dy * dy);
            if (distance < bestDistance - 1.0e-9) {
                bestDistance = distance;
                best = &c;
            } else if (std::abs(distance - bestDistance) <= 1.0e-9) {
                if (!best || c.integratedIntensity > best->integratedIntensity)
                    best = &c;
            }
        }
    } else {
        // Without history select the strongest valid candidate.
        for (const auto &c : candidates) {
            if (!best || c.integratedIntensity > best->integratedIntensity)
                best = &c;
        }
        result.diagnostic = QStringLiteral("无历史跟踪记录，选择最强候选");
    }

    // Small-kernel intensity-weighted centroid around the component center.
    const int radius = config_.smallKernelRadiusPx;
    const int cx = qBound(0, qRound(best->centroidX), roiMono8.cols - 1);
    const int cy = qBound(0, qRound(best->centroidY), roiMono8.rows - 1);

    double sumWeight = 0.0;
    double sumX = 0.0;
    double sumY = 0.0;
    double peakValue = 0.0;
    for (int y = std::max(0, cy - radius);
         y <= std::min(roiMono8.rows - 1, cy + radius); ++y) {
        for (int x = std::max(0, cx - radius);
             x <= std::min(roiMono8.cols - 1, cx + radius); ++x) {
            // The reference small-kernel method uses the original positive
            // Mono8 intensity as the weight; no dark-field/hot-pixel template
            // or local-background subtraction is migrated into KY-DIMM.
            const double weight = static_cast<double>(
                roiMono8.at<std::uint8_t>(y, x));
            sumWeight += weight;
            sumX += weight * static_cast<double>(x);
            sumY += weight * static_cast<double>(y);
            peakValue = std::max(
                peakValue, static_cast<double>(roiMono8.at<std::uint8_t>(y, x)));
        }
    }

    result.componentAreaPx = best->area;
    if (sumWeight <= config_.minimumCentroidIntensity) {
        result.valid = false;
        result.diagnostic = QStringLiteral("小核加权强度不足");
    } else {
        result.centroidPx = QPointF(sumX / sumWeight, sumY / sumWeight);
        result.integratedIntensity = sumWeight;
        result.peakIntensity = peakValue;
        result.valid = true;
    }
    return result;
}
