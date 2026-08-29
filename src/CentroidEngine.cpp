#include "CentroidEngine.h"
#include "ConnectedDomain.h"
#include "StarSegmentation.h"

#include <opencv2/imgproc.hpp>

#include <QtGlobal>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace {

struct ComponentCandidate {
    int label = 0;
    double centroidX = 0.0;
    double centroidY = 0.0;
    int area = 0;
    double integratedIntensity = 0.0;
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

CentroidMeasurement CentroidEngine::locate(
    const cv::Mat &mono8, const QPointF *expectedLocalCentroid) const
{
    CentroidMeasurement result;
    CV_Assert(mono8.type() == CV_8UC1);
    if (mono8.empty()) {
        result.diagnostic = QStringLiteral("图像为空");
        return result;
    }

    const StarSegmentation::ForegroundSegmentation segmentation =
        StarSegmentation::segmentForegroundOtsu(
            mono8, config_.otsuSigmaThreshold, config_.otsuPeakFraction);
    if (!segmentation.valid) {
        result.diagnostic = QStringLiteral("Otsu 分割失败");
        return result;
    }

    cv::Mat labels;
    cv::Mat stats;
    cv::Mat centroids;
    const int componentCount = cv::connectedComponentsWithStats(
        segmentation.mask, labels, stats, centroids,
        ConnectedDomain::sanitizeConnectivity(config_.connectivity), CV_32S);
    if (componentCount <= 1) {
        result.diagnostic = QStringLiteral("未找到连通域");
        return result;
    }

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

    std::vector<ComponentCandidate> candidates;
    for (int label = 1; label < componentCount; ++label) {
        const int area = stats.at<int>(label, cv::CC_STAT_AREA);
        if (area < config_.otsuMinimumComponentAreaPx ||
            area > config_.otsuMaximumComponentAreaPx)
            continue;
        ComponentCandidate candidate;
        candidate.label = label;
        candidate.centroidX = centroids.at<double>(label, 0);
        candidate.centroidY = centroids.at<double>(label, 1);
        candidate.area = area;
        candidate.integratedIntensity = integrated[label];
        candidates.push_back(candidate);
    }
    if (candidates.empty()) {
        result.diagnostic = QStringLiteral("未找到有效连通域");
        return result;
    }

    const ComponentCandidate *best = nullptr;
    if (expectedLocalCentroid) {
        double bestDistance = std::numeric_limits<double>::max();
        for (const ComponentCandidate &candidate : candidates) {
            const double dx = candidate.centroidX - expectedLocalCentroid->x();
            const double dy = candidate.centroidY - expectedLocalCentroid->y();
            const double candidateDistance = std::hypot(dx, dy);
            if (candidateDistance < bestDistance - 1.0e-9 ||
                (std::abs(candidateDistance - bestDistance) <= 1.0e-9 &&
                 (!best || candidate.integratedIntensity >
                                best->integratedIntensity))) {
                bestDistance = candidateDistance;
                best = &candidate;
            }
        }
    } else {
        for (const ComponentCandidate &candidate : candidates) {
            if (!best || candidate.integratedIntensity >
                              best->integratedIntensity)
                best = &candidate;
        }
        result.diagnostic = QStringLiteral("无历史跟踪记录，选择最强候选");
    }

    result = refineInKernel(mono8,
                            QPointF(best->centroidX, best->centroidY));
    result.componentAreaPx = best->area;
    return result;
}

CentroidMeasurement CentroidEngine::refineInKernel(
    const cv::Mat &mono8, const QPointF &coarseLocalCentroid) const
{
    CentroidMeasurement result;
    CV_Assert(mono8.type() == CV_8UC1);
    if (mono8.empty()) {
        result.diagnostic = QStringLiteral("质心核输入为空");
        return result;
    }

    const int radius = config_.centroidKernelRadiusPx;
    if (radius < 1) {
        result.diagnostic = QStringLiteral("质心核半径无效");
        return result;
    }
    const int cx = qBound(0, qRound(coarseLocalCentroid.x()), mono8.cols - 1);
    const int cy = qBound(0, qRound(coarseLocalCentroid.y()), mono8.rows - 1);

    double sumWeight = 0.0;
    double sumX = 0.0;
    double sumY = 0.0;
    double peakValue = 0.0;
    for (int row = cy - radius; row <= cy + radius; ++row) {
        if (row < 0 || row >= mono8.rows)
            continue;
        for (int column = cx - radius; column <= cx + radius; ++column) {
            if (column < 0 || column >= mono8.cols)
                continue;
            // The configured window is the only refinement aperture. Hot-pixel
            // and saturation-template handling is intentionally not applied.
            const double weight = static_cast<double>(
                mono8.at<std::uint8_t>(row, column));
            sumWeight += weight;
            sumX += weight * static_cast<double>(column);
            sumY += weight * static_cast<double>(row);
            peakValue = std::max(peakValue, weight);
        }
    }
    if (sumWeight <= config_.minimumCentroidIntensity) {
        result.diagnostic = QStringLiteral("质心核加权强度不足");
        return result;
    }
    result.centroidPx = QPointF(sumX / sumWeight, sumY / sumWeight);
    result.integratedIntensity = sumWeight;
    result.peakIntensity = peakValue;
    result.valid = true;
    return result;
}
