#pragma once

#include <QMetaType>
#include <QRect>
#include <QString>
#include <opencv2/core.hpp>

#include <cstdint>

enum class PixelFormat {
    Mono8,
    Unknown
};

enum class TriggerMode {
    Continuous,
    Software,
    Hardware
};

struct RoiRect {
    int x = 0;
    int y = 0;
    int width = 64;
    int height = 64;

    QRect toQRect() const { return QRect(x, y, width, height); }
    bool isValid() const { return width > 0 && height > 0; }
};

struct CameraFrame {
    cv::Mat mono8;
    QRect sourceRect{0, 0, 1920, 1200};
    std::uint64_t sequence = 0;
    std::int64_t timestampNs = 0;
    PixelFormat pixelFormat = PixelFormat::Unknown;
    std::uint64_t configurationGeneration = 0;

    bool isValid() const
    {
        return !mono8.empty() && mono8.type() == CV_8UC1 &&
               sourceRect.width() == mono8.cols &&
               sourceRect.height() == mono8.rows &&
               pixelFormat == PixelFormat::Mono8;
    }
};

struct CameraCapabilities {
    QString modelName;
    QString serialNumber;
    int sensorWidth = 1936;
    int sensorHeight = 1216;
    int defaultWidth = 1920;
    int defaultHeight = 1200;
    double pixelSizeUm = 5.86;
    double nominalFrameRateHz = 42.0;
    bool supportsMono8 = false;
    bool supportsSoftwareTrigger = false;
    bool supportsHardwareTrigger = false;
    bool supportsHardwareAoi = false;
};

struct CameraStatistics {
    std::uint64_t receivedFrames = 0;
    std::uint64_t droppedFrames = 0;
    std::uint64_t queueDroppedFrames = 0;
    double measuredRateHz = 0.0;
    double averageCallbackMs = 0.0;
    bool connected = false;
    QString lastError;
};

Q_DECLARE_METATYPE(RoiRect)
Q_DECLARE_METATYPE(CameraCapabilities)
Q_DECLARE_METATYPE(CameraStatistics)
