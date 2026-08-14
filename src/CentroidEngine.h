#pragma once

#include "ProcessingTypes.h"

#include <opencv2/core.hpp>

// Reference centroid method: Otsu thresholding, 4-connected components, then a
// small-kernel intensity-weighted centroid. Only this path is implemented.
class CentroidEngine final {
public:
    explicit CentroidEngine(ProcessingConfig config);

    void setConfig(ProcessingConfig config);
    CentroidMeasurement locate(const cv::Mat &roiMono8,
                               const QPointF *expectedLocalCentroid = nullptr) const;

private:
    ProcessingConfig config_;
};
