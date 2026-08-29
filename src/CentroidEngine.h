#pragma once

#include "ProcessingTypes.h"

#include <opencv2/core.hpp>

// Reference centroid method: Otsu thresholding, connected components, then the
// UI_2-compatible intensity-weighted centroid in a configurable odd kernel.
class CentroidEngine final {
public:
    explicit CentroidEngine(ProcessingConfig config);

    void setConfig(ProcessingConfig config);
    CentroidMeasurement locate(const cv::Mat &mono8,
                               const QPointF *expectedLocalCentroid = nullptr) const;
    CentroidMeasurement refineInKernel(const cv::Mat &mono8,
                                       const QPointF &coarseLocalCentroid) const;

private:
    ProcessingConfig config_;
};
