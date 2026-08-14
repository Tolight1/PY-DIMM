#pragma once

#include "CameraTypes.h"
#include "ProcessingTypes.h"

#include <QImage>

class ImageDisplayAdapter final {
public:
    static QImage toGrayImage(const cv::Mat &mono8);
    static QImage drawOverlay(const QImage &base,
                              const RoiOverlay &overlay,
                              const QRect &fullFrameRect);
};
