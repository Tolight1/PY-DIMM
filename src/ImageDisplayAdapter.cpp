#include "ImageDisplayAdapter.h"

#include <opencv2/core.hpp>

#include <QPainter>
#include <QPen>

namespace {

// Maps full-frame image coordinates into the target display rectangle. When
// the target equals the image rect, this is the identity mapping.
QRectF mapRect(const QRectF &source, const QSize &imageSize,
               const QRect &target)
{
    if (imageSize.isEmpty() || target.isEmpty())
        return source;
    const double sx = static_cast<double>(target.width()) /
                      static_cast<double>(imageSize.width());
    const double sy = static_cast<double>(target.height()) /
                      static_cast<double>(imageSize.height());
    return QRectF(source.x() * sx + target.x(), source.y() * sy + target.y(),
                  source.width() * sx, source.height() * sy);
}

QPointF mapPoint(const QPointF &source, const QSize &imageSize,
                 const QRect &target)
{
    if (imageSize.isEmpty() || target.isEmpty())
        return source;
    const double sx = static_cast<double>(target.width()) /
                      static_cast<double>(imageSize.width());
    const double sy = static_cast<double>(target.height()) /
                      static_cast<double>(imageSize.height());
    return QPointF(source.x() * sx + target.x(), source.y() * sy + target.y());
}

} // namespace

QImage ImageDisplayAdapter::toGrayImage(const cv::Mat &mono8)
{
    if (mono8.empty() || mono8.type() != CV_8UC1)
        return QImage();
    return QImage(mono8.data, mono8.cols, mono8.rows,
                  static_cast<int>(mono8.step), QImage::Format_Grayscale8)
        .copy();
}

QImage ImageDisplayAdapter::drawOverlay(const QImage &base,
                                        const RoiOverlay &overlay,
                                        const QRect &fullFrameRect)
{
    if (base.isNull())
        return QImage();
    QImage image = base;

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);

    const QSize imageSize = base.size();

    if (overlay.hasHardwareAoi) {
        QPen aoiPen(QColor(0, 200, 255, 230));
        aoiPen.setWidth(2);
        painter.setPen(aoiPen);
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(mapRect(overlay.hardwareAoi.toQRect(), imageSize,
                                 fullFrameRect));
    }

    if (overlay.hasRois) {
        QPen roiPen(QColor(255, 220, 80, 230));
        roiPen.setWidth(1);
        painter.setPen(roiPen);
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(
            mapRect(overlay.roiA.toQRect(), imageSize, fullFrameRect));
        painter.drawRect(
            mapRect(overlay.roiB.toQRect(), imageSize, fullFrameRect));
    }

    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 255, 140, 255));
    const QPointF starA = mapPoint(overlay.starA, imageSize, fullFrameRect);
    const QPointF starB = mapPoint(overlay.starB, imageSize, fullFrameRect);
    painter.drawEllipse(starA, 4.0, 4.0);
    painter.drawEllipse(starB, 4.0, 4.0);

    return image;
}
