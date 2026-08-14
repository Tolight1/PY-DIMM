#include "StarSegmentation.h"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>

namespace StarSegmentation {

ForegroundSegmentation segmentForegroundOtsu(const cv::Mat &image,
                                              double sigmaThreshold,
                                              double peakFraction)
{
    ForegroundSegmentation output;
    if (image.empty() || image.type() != CV_8UC1)
        return output;

    cv::Mat nativeMask;
    const double otsuThreshold = cv::threshold(
        image, nativeMask, 0.0, 255.0, cv::THRESH_BINARY | cv::THRESH_OTSU);
    output.otsuThreshold = otsuThreshold;

    cv::Scalar meanValue;
    cv::Scalar standardDeviationValue;
    cv::meanStdDev(image, meanValue, standardDeviationValue);
    const double mean = meanValue[0];
    const double standardDeviation = standardDeviationValue[0];
    double minimum = 0.0;
    double maximum = 0.0;
    cv::minMaxLoc(image, &minimum, &maximum);

    sigmaThreshold = std::max(0.0, sigmaThreshold);
    peakFraction = std::clamp(peakFraction, 0.0, 1.0);
    const double sigmaFloor = mean + sigmaThreshold * standardDeviation;
    const double peakFloor = mean + peakFraction * (maximum - mean);
    double actualThreshold = std::max(otsuThreshold, sigmaFloor);
    actualThreshold = std::max(actualThreshold, peakFloor);
    if (actualThreshold >= maximum && maximum > minimum)
        actualThreshold = std::nextafter(maximum, minimum);

    output.actualThreshold = actualThreshold;
    cv::threshold(image, output.mask, actualThreshold, 255.0, cv::THRESH_BINARY);
    output.valid = !output.mask.empty();
    return output;
}

} // namespace StarSegmentation
