#include "PylonCamera.h"

#include <pylon/PylonIncludes.h>
#include <pylon/BaslerUniversalInstantCamera.h>
#include <pylon/InstantCamera.h>
#include <GenApi/GenApi.h>

#include <opencv2/core.hpp>

#include <QMutex>
#include <QMutexLocker>

#include <chrono>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <functional>

namespace {

// PylonInitialize() exactly once for the application lifetime; PylonTerminate()
// runs after all camera objects have been destroyed (namespace-scope object is
// destroyed at program exit, after MainWindow's workers and cameras).
struct PylonLifecycleGuard {
    PylonLifecycleGuard() { Pylon::PylonInitialize(); }
    ~PylonLifecycleGuard() { Pylon::PylonTerminate(); }
};
const PylonLifecycleGuard g_pylonLifecycle;

using namespace Pylon;

// Generic node-availability probe. Some devices do not expose every node;
// a missing node must never crash configuration.
bool nodeExists(CInstantCamera &camera, const char *name)
{
    try {
        return camera.GetNodeMap().GetNode(name) != nullptr;
    } catch (...) {
        return false;
    }
}

std::int64_t nowNs()
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

} // namespace

class PylonCamera::Impl {
public:
    class FrameHandler : public CBaslerUniversalImageEventHandler {
    public:
        explicit FrameHandler(Impl *owner)
            : owner_(owner)
        {
        }
        void OnImageGrabbed(CBaslerUniversalInstantCamera &,
                            const CBaslerUniversalGrabResultPtr &grabResult) override
        {
            owner_->handleFrame(grabResult);
        }
        void OnImagesSkipped(CBaslerUniversalInstantCamera &,
                             size_t countOfSkippedImages) override
        {
            owner_->dropped_.fetch_add(countOfSkippedImages, std::memory_order_relaxed);
        }

    private:
        Impl *owner_;
    };

    // Use the universal camera wrapper because the generated parameter
    // interface (PixelFormat, Width, OffsetX, TriggerMode, ...) is otherwise
    // not available on a plain CInstantCamera.
    CBaslerUniversalInstantCamera camera_;
    FrameHandler frameHandler_{this};
    std::function<void(CameraFrame)> callback_;

    std::atomic<std::uint64_t> sequence_{0};
    std::atomic<std::uint64_t> dropped_{0};
    std::uint64_t configurationGeneration_ = 0;
    QRect currentSourceRect_{0, 0, 1920, 1200};

    mutable QMutex statsMutex_;
    CameraStatistics stats_;

    bool open_ = false;
    bool grabbing_ = false;
    bool mono8Configured_ = false;
    double averageCallbackSumMs_ = 0.0;
    std::uint64_t callbackCount_ = 0;

    ~Impl() { close(); }

    void close()
    {
        if (camera_.IsGrabbing())
            camera_.StopGrabbing();
        grabbing_ = false;
        if (camera_.IsOpen())
            camera_.DeregisterImageEventHandler(&frameHandler_);
        if (camera_.IsOpen())
            camera_.Close();
        open_ = false;
        QMutexLocker lock(&statsMutex_);
        stats_.connected = false;
    }

    void startGrabbingInternal()
    {
        camera_.RegisterImageEventHandler(&frameHandler_, RegistrationMode_ReplaceAll, Cleanup_None);
        camera_.StartGrabbing(GrabStrategy_LatestImageOnly, GrabLoop_ProvidedByInstantCamera);
        grabbing_ = true;
    }

    void handleFrame(const CBaslerUniversalGrabResultPtr &grabResult)
    {
        const std::int64_t t0 = nowNs();
        if (!grabResult->GrabSucceeded()) {
            dropped_.fetch_add(1, std::memory_order_relaxed);
            return;
        }

        const std::size_t width = static_cast<std::size_t>(grabResult->GetWidth());
        const std::size_t height = static_cast<std::size_t>(grabResult->GetHeight());
        if (width == 0 || height == 0 || !grabResult->GetBuffer())
            return;

        CameraFrame frame;
        frame.mono8 = cv::Mat(static_cast<int>(height), static_cast<int>(width), CV_8UC1);
        std::size_t stride = width;
        grabResult->GetStride(stride);
        for (std::size_t y = 0; y < height; ++y) {
            std::memcpy(frame.mono8.ptr(static_cast<int>(y)),
                        static_cast<const std::uint8_t *>(grabResult->GetBuffer()) +
                            y * stride,
                        width);
        }
        {
            QMutexLocker lock(&statsMutex_);
            frame.sourceRect = currentSourceRect_;
            frame.configurationGeneration = configurationGeneration_;
        }
        frame.sequence = sequence_.fetch_add(1, std::memory_order_relaxed);
        // GetTimeStamp() is a camera-specific tick counter, not guaranteed to
        // be nanoseconds. Use a monotonic host timestamp for rate and tau0.
        frame.timestampNs = nowNs();
        frame.pixelFormat = PixelFormat::Mono8;

        // Only metadata statistics are updated here; no image processing, no
        // file writes, and no Qt widget calls happen on the grab thread.
        {
            QMutexLocker lock(&statsMutex_);
            ++stats_.receivedFrames;
            const double elapsedMs = static_cast<double>(nowNs() - t0) * 1.0e-6;
            averageCallbackSumMs_ += elapsedMs;
            ++callbackCount_;
            stats_.averageCallbackMs = averageCallbackSumMs_ /
                                       static_cast<double>(callbackCount_);
            stats_.measuredRateHz = 0.0; // measured in MeasurementWorker
        }

        if (callback_)
            callback_(frame);
    }
};

PylonCamera::PylonCamera()
    : impl_(new Impl())
{
}

PylonCamera::~PylonCamera()
{
    close();
}

bool PylonCamera::openFirstCompatibleCamera(QString *error)
{
    try {
        if (impl_->camera_.IsOpen())
            return true;

        CTlFactory &factory = CTlFactory::GetInstance();
        DeviceInfoList_t devices;
        factory.EnumerateDevices(devices);
        if (devices.empty()) {
            if (error)
                *error = QStringLiteral("未找到任何相机设备");
            return false;
        }

        // This project is calibrated and tested for the acA1920-40gm. Do not
        // silently attach another enumerated camera with different geometry.
        const CDeviceInfo *chosen = nullptr;
        for (const auto &info : devices) {
            if (QString::fromUtf8(info.GetModelName().c_str()) == QStringLiteral("acA1920-40gm")) {
                chosen = &info;
                break;
            }
        }
        if (!chosen) {
            if (error)
                *error = QStringLiteral("未找到 Basler acA1920-40gm 相机");
            return false;
        }
        const CDeviceInfo &target = *chosen;

        impl_->camera_.Attach(factory.CreateDevice(target));
        impl_->camera_.Open();
        impl_->open_ = true;
        {
            QMutexLocker lock(&impl_->statsMutex_);
            impl_->stats_.connected = true;
            impl_->stats_.lastError.clear();
        }
        return true;
    } catch (const GenericException &e) {
        if (error)
            *error = QString::fromUtf8(e.GetDescription());
        return false;
    }
}

void PylonCamera::close()
{
    impl_->close();
}

bool PylonCamera::isOpen() const
{
    return impl_->open_ && impl_->camera_.IsOpen();
}

CameraCapabilities PylonCamera::capabilities() const
{
    CameraCapabilities caps;
    if (!isOpen())
        return caps;
    try {
        const CDeviceInfo &deviceInfo = impl_->camera_.GetDeviceInfo();
        caps.modelName = QString::fromUtf8(deviceInfo.GetModelName().c_str());
        caps.serialNumber = QString::fromUtf8(deviceInfo.GetSerialNumber().c_str());
        caps.sensorWidth = nodeExists(impl_->camera_, "SensorWidth")
            ? impl_->camera_.SensorWidth.GetValue() : 1936;
        caps.sensorHeight = nodeExists(impl_->camera_, "SensorHeight")
            ? impl_->camera_.SensorHeight.GetValue() : 1216;
        caps.defaultWidth = 1920;
        caps.defaultHeight = 1200;
        caps.pixelSizeUm = 5.86;
        caps.nominalFrameRateHz = 42.0;
        caps.supportsMono8 = impl_->mono8Configured_;
        caps.supportsSoftwareTrigger = nodeExists(impl_->camera_, "TriggerSource");
        caps.supportsHardwareTrigger = nodeExists(impl_->camera_, "LineSelector");
        caps.supportsHardwareAoi = nodeExists(impl_->camera_, "Width") &&
                                   nodeExists(impl_->camera_, "OffsetX");
    } catch (const GenericException &) {
        // keep the defaults on any node access error
    }
    return caps;
}

CameraStatistics PylonCamera::statistics() const
{
    QMutexLocker lock(&impl_->statsMutex_);
    CameraStatistics copy = impl_->stats_;
    copy.droppedFrames = impl_->dropped_.load(std::memory_order_relaxed);
    return copy;
}

bool PylonCamera::configureMono8(QString *error)
{
    try {
        if (!isOpen()) {
            if (error)
                *error = QStringLiteral("相机未打开");
            return false;
        }
        if (!nodeExists(impl_->camera_, "PixelFormat")) {
            if (error)
                *error = QStringLiteral("相机不支持 PixelFormat 节点");
            return false;
        }
        // Mono8 only. Do not fall back to Mono16.
        impl_->camera_.PixelFormat.SetValue("Mono8");
        impl_->mono8Configured_ = true;
        return true;
    } catch (const GenericException &e) {
        if (error)
            *error = QString::fromUtf8(e.GetDescription());
        return false;
    }
}

bool PylonCamera::configureExposure(double exposureTimeMs, QString *error)
{
    try {
        if (!isOpen()) {
            if (error)
                *error = QStringLiteral("相机未打开");
            return false;
        }
        if (exposureTimeMs <= 0.0 || exposureTimeMs > 10.0) {
            if (error)
            *error = QStringLiteral("单次曝光时间必须在 (0, 10] ms 范围内");
            return false;
        }
        // Basler exposure nodes are expressed in microseconds.
        const double exposureUs = exposureTimeMs * 1000.0;
        if (nodeExists(impl_->camera_, "ExposureTime"))
            impl_->camera_.ExposureTime.SetValue(exposureUs);
        else if (nodeExists(impl_->camera_, "ExposureTimeAbs"))
            impl_->camera_.ExposureTimeAbs.SetValue(exposureUs);
        else {
            if (error)
                *error = QStringLiteral("相机不支持 ExposureTime 节点");
            return false;
        }
        return true;
    } catch (const GenericException &e) {
        if (error)
            *error = QString::fromUtf8(e.GetDescription());
        return false;
    }
}

bool PylonCamera::configureAcquisitionRate(double frameRateHz, QString *error)
{
    try {
        if (!isOpen()) {
            if (error)
                *error = QStringLiteral("相机未打开");
            return false;
        }
        if (frameRateHz <= 0.0) {
            if (error)
                *error = QStringLiteral("采样帧率必须大于 0 Hz");
            return false;
        }

        GenApi::INodeMap &nodeMap = impl_->camera_.GetNodeMap();
        GenApi::CBooleanPtr rateEnable(nodeMap.GetNode("AcquisitionFrameRateEnable"));
        if (GenApi::IsWritable(rateEnable))
            rateEnable->SetValue(true);

        GenApi::CFloatPtr rate(nodeMap.GetNode("AcquisitionFrameRate"));
        if (!GenApi::IsWritable(rate))
            rate = GenApi::CFloatPtr(nodeMap.GetNode("AcquisitionFrameRateAbs"));
        if (GenApi::IsWritable(rate)) {
            const double minimum = rate->GetMin();
            const double maximum = rate->GetMax();
            const double accepted = std::clamp(frameRateHz, minimum, maximum);
            rate->SetValue(accepted);
            if (std::abs(accepted - frameRateHz) > 0.01 && error) {
            *error = QStringLiteral("相机采集帧率范围为 %1～%2 Hz，无法完全达到 %3 Hz")
                             .arg(minimum, 0, 'f', 1)
                             .arg(maximum, 0, 'f', 1)
                             .arg(frameRateHz, 0, 'f', 1);
                return false;
            }
            return true;
        }

        if (error)
            *error = QStringLiteral("相机不支持 AcquisitionFrameRate 节点");
        return false;
    } catch (const GenericException &e) {
        if (error)
            *error = QString::fromUtf8(e.GetDescription());
        return false;
    }
}

bool PylonCamera::configureTrigger(const TriggerConfig &config, QString *error)
{
    try {
        if (!isOpen()) {
            if (error)
                *error = QStringLiteral("相机未打开");
            return false;
        }
        if (config.triggerSelectorFrameStart)
            impl_->camera_.TriggerSelector.SetValue("FrameStart");

        switch (config.mode) {
        case TriggerMode::Continuous:
            // Free-run mode.
            impl_->camera_.TriggerMode.SetValue("Off");
            break;
        case TriggerMode::Software:
            impl_->camera_.TriggerMode.SetValue("On");
            impl_->camera_.TriggerSource.SetValue("Software");
            break;
        case TriggerMode::Hardware:
            impl_->camera_.TriggerMode.SetValue("On");
            impl_->camera_.TriggerSource.SetValue(config.hardwareTriggerLine.toStdString().c_str());
            break;
        }
        return true;
    } catch (const GenericException &e) {
        if (error)
            *error = QString::fromUtf8(e.GetDescription());
        return false;
    }
}

namespace {
RoiRect alignAoiToIncrements(const RoiRect &aoi, int sensorWidth, int sensorHeight,
                             std::int64_t incX, std::int64_t incY,
                             std::int64_t incW, std::int64_t incH)
{
    RoiRect out;
    const std::int64_t incXv = std::max<std::int64_t>(incX, 1);
    const std::int64_t incYv = std::max<std::int64_t>(incY, 1);
    const std::int64_t incWv = std::max<std::int64_t>(incW, 1);
    const std::int64_t incHv = std::max<std::int64_t>(incH, 1);

    out.x = static_cast<int>((static_cast<std::int64_t>(aoi.x) / incXv) * incXv);
    out.y = static_cast<int>((static_cast<std::int64_t>(aoi.y) / incYv) * incYv);
    out.width = static_cast<int>(std::max<std::int64_t>(
        incWv, (static_cast<std::int64_t>(aoi.width) / incWv) * incWv));
    out.height = static_cast<int>(std::max<std::int64_t>(
        incHv, (static_cast<std::int64_t>(aoi.height) / incHv) * incHv));

    // Clamp to the sensor bounds.
    if (out.x < 0) out.x = 0;
    if (out.y < 0) out.y = 0;
    if (out.x + out.width > sensorWidth) out.width = sensorWidth - out.x;
    if (out.y + out.height > sensorHeight) out.height = sensorHeight - out.y;
    if (out.width < 1) out.width = 1;
    if (out.height < 1) out.height = 1;
    return out;
}
} // namespace

bool PylonCamera::configureHardwareAoi(const RoiRect &aoi, QString *error)
{
    try {
        if (!isOpen()) {
            if (error)
                *error = QStringLiteral("相机未打开");
            return false;
        }

        const int sensorW = nodeExists(impl_->camera_, "SensorWidth")
            ? static_cast<int>(impl_->camera_.SensorWidth.GetValue()) : 1936;
        const int sensorH = nodeExists(impl_->camera_, "SensorHeight")
            ? static_cast<int>(impl_->camera_.SensorHeight.GetValue()) : 1216;

        RoiRect aligned = alignAoiToIncrements(
            aoi, sensorW, sensorH,
            impl_->camera_.OffsetX.GetInc(),
            impl_->camera_.OffsetY.GetInc(),
            impl_->camera_.Width.GetInc(),
            impl_->camera_.Height.GetInc());

        const bool wasGrabbing = impl_->camera_.IsGrabbing();
        if (wasGrabbing) {
            impl_->camera_.StopGrabbing();
            impl_->grabbing_ = false;
        }

        // One enclosing hardware AOI. Width/Height first, then offsets, so the
        // requested rectangle always fits within the sensor.
        impl_->camera_.Width.SetValue(aligned.width);
        impl_->camera_.Height.SetValue(aligned.height);
        impl_->camera_.OffsetX.SetValue(aligned.x);
        impl_->camera_.OffsetY.SetValue(aligned.y);

        // Read back the values the camera actually accepted.
        {
            QMutexLocker lock(&impl_->statsMutex_);
            impl_->currentSourceRect_ = QRect(
                static_cast<int>(impl_->camera_.OffsetX.GetValue()),
                static_cast<int>(impl_->camera_.OffsetY.GetValue()),
                static_cast<int>(impl_->camera_.Width.GetValue()),
                static_cast<int>(impl_->camera_.Height.GetValue()));
            ++impl_->configurationGeneration_;
        }

        if (wasGrabbing)
            impl_->startGrabbingInternal();
        return true;
    } catch (const GenericException &e) {
        impl_->grabbing_ = impl_->camera_.IsGrabbing();
        if (error)
            *error = QString::fromUtf8(e.GetDescription());
        return false;
    }
}

bool PylonCamera::resetToFullFrame(QString *error)
{
    try {
        if (!isOpen()) {
            if (error)
                *error = QStringLiteral("相机未打开");
            return false;
        }

        const bool wasGrabbing = impl_->camera_.IsGrabbing();
        if (wasGrabbing) {
            impl_->camera_.StopGrabbing();
            impl_->grabbing_ = false;
        }

        // Back to the default full-frame 1920 × 1200 acquisition canvas.
        impl_->camera_.OffsetX.TrySetToMinimum();
        impl_->camera_.OffsetY.TrySetToMinimum();
        impl_->camera_.Width.SetValue(1920);
        impl_->camera_.Height.SetValue(1200);

        {
            QMutexLocker lock(&impl_->statsMutex_);
            impl_->currentSourceRect_ = QRect(0, 0, 1920, 1200);
            impl_->configurationGeneration_ = 0;
        }

        if (wasGrabbing)
            impl_->startGrabbingInternal();
        return true;
    } catch (const GenericException &e) {
        impl_->grabbing_ = impl_->camera_.IsGrabbing();
        if (error)
            *error = QString::fromUtf8(e.GetDescription());
        return false;
    }
}

bool PylonCamera::startGrabbing(QString *error)
{
    try {
        if (!isOpen()) {
            if (error)
                *error = QStringLiteral("相机未打开");
            return false;
        }
        if (impl_->grabbing_)
            return true;
        impl_->startGrabbingInternal();
        return true;
    } catch (const GenericException &e) {
        if (error)
            *error = QString::fromUtf8(e.GetDescription());
        return false;
    }
}

void PylonCamera::stopGrabbing()
{
    try {
        if (impl_->camera_.IsGrabbing())
            impl_->camera_.StopGrabbing();
        impl_->grabbing_ = false;
    } catch (const GenericException &) {
        impl_->grabbing_ = false;
    }
}

bool PylonCamera::executeSoftwareTrigger(QString *error)
{
    try {
        if (!isOpen()) {
            if (error)
                *error = QStringLiteral("相机未打开");
            return false;
        }
        if (!impl_->camera_.IsGrabbing()) {
            if (error)
                *error = QStringLiteral("相机当前未在采集");
            return false;
        }
        impl_->camera_.ExecuteSoftwareTrigger();
        return true;
    } catch (const GenericException &e) {
        if (error)
            *error = QString::fromUtf8(e.GetDescription());
        return false;
    }
}

void PylonCamera::setFrameCallback(std::function<void(CameraFrame)> callback)
{
    impl_->callback_ = std::move(callback);
}

std::uint64_t PylonCamera::configurationGeneration() const
{
    QMutexLocker lock(&impl_->statsMutex_);
    return impl_->configurationGeneration_;
}
