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
                                         const DisplayOverlay &overlay,
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

    if (overlay.hasCentroids) {
        QPen centroidPen(QColor(255, 70, 70, 255));
        centroidPen.setWidthF(1.5);
        painter.setPen(centroidPen);
        painter.setBrush(Qt::NoBrush);
        const auto drawCross = [&](const QPointF &source) {
            const QPointF center = mapPoint(source, imageSize, fullFrameRect);
            const double arm = 8.0;
            const auto drawLines = [&]() {
                painter.drawLine(QPointF(center.x() - arm, center.y()),
                                 QPointF(center.x() + arm, center.y()));
                painter.drawLine(QPointF(center.x(), center.y() - arm),
                                 QPointF(center.x(), center.y() + arm));
            };
            QPen outlinePen(QColor(0, 0, 0, 220));
            outlinePen.setWidthF(4.5);
            painter.setPen(outlinePen);
            drawLines();
            centroidPen.setWidthF(2.0);
            painter.setPen(centroidPen);
            drawLines();
        };
        drawCross(overlay.starA);
        drawCross(overlay.starB);
    }

    return image;
}
