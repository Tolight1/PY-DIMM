#pragma once

#include <opencv2/core.hpp>

namespace StarSegmentation {

struct ForegroundSegmentation {
    bool valid = false;
    double otsuThreshold = 0.0;
    double actualThreshold = 0.0;
    cv::Mat mask;
};

ForegroundSegmentation segmentForegroundOtsu(const cv::Mat &image,
                                              double sigmaThreshold,
                                              double peakFraction);

} // namespace StarSegmentation
