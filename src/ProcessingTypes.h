#pragma once

#include "CameraTypes.h"

#include <QMetaType>
#include <QPointF>
#include <QString>
#include <QVector>

#include <cstdint>

struct OpticalConfig {
    double mainTelescopeApertureMm = 254.0;
    double subApertureDiameterMm = 60.0;
    double baselineSeparationMm = 150.0;
    double focalLengthMm = 2500.0;
    double wavelengthNm = 550.0;
    double pixelSizeUm = 5.86;
    double zenithAngleDeg = 0.0;
};

struct AcquisitionConfig {
    int frameWidth = 1920;
    int frameHeight = 1200;
    PixelFormat pixelFormat = PixelFormat::Mono8;
    double measurementRateHz = 100.0;
    double fullFramePreviewRateHz = 3.0;
    double roiPreviewRateHz = 20.0;
    double exposureTimeMs = 5.0;
    int targetSampleCount = 2000;
    double targetDurationSec = 20.0;
    bool enableHardwareAoi = true;
};

struct ProcessingConfig {
    int roiWidthPx = 64;
    int roiHeightPx = 64;
    int hardwareAoiMarginPx = 64;

    // Kept for backwards-compatible settings files. Native OpenCV Otsu does
    // not use a configurable histogram-bin count.
    int otsuHistogramBins = 256;
    double otsuSigmaThreshold = 4.0;
    double otsuPeakFraction = 0.20;
    int connectivity = 8;
    int otsuMinimumComponentAreaPx = 9;
    int otsuMaximumComponentAreaPx = 1000;
    int smallKernelRadiusPx = 3;
    double minimumPeakDistancePx = 8.0;
    double minimumCentroidIntensity = 1.0;

    int roiRecenteringDistanceToEdgePx = 16;
    int roiRecenteringConsecutiveFrames = 5;
    int roiRecenteringCooldownMs = 3000;
    double roiRecenteringMinimumShiftPx = 8.0;
    int roiLostRelocalizationFrames = 10;

    int r0WindowFrames = 1000;
    double resultUpdateIntervalSec = 1.0;
    int tau0HistorySeconds = 3;
    double tau0MaximumLagMs = 200.0;
    int tau0MinimumSamples = 30;
};

struct TriggerConfig {
    TriggerMode mode = TriggerMode::Continuous;
    QString hardwareTriggerLine = QStringLiteral("Line1");
    bool triggerSelectorFrameStart = true;
    bool softwareTriggerEachFrame = true;
    bool allowPartialScan = false;
};

struct StorageConfig {
    QString outputDirectory = QStringLiteral("data");
    double resultRecordIntervalSec = 1.0;
    bool saveParameterCsv = true;
    bool saveCentroidCsv = true;
    bool saveDiagnosticsCsv = true;
    bool saveRunMetadataJson = true;
    bool imageSavingFixedOff = true;
};

struct UiConfig {
    bool showFullFramePreview = true;
    bool showRoiPreview = true;
    bool drawHardwareAoi = true;
    bool drawSoftwareRois = true;
};

struct CentroidMeasurement {
    QPointF centroidPx;
    double peakIntensity = 0.0;
    double integratedIntensity = 0.0;
    int componentAreaPx = 0;
    bool valid = false;
    QString diagnostic;
};

struct TwoStarMeasurement {
    CentroidMeasurement starA;
    CentroidMeasurement starB;
    RoiRect roiA;
    RoiRect roiB;
    QPointF fullFrameStarA;
    QPointF fullFrameStarB;
    bool validPair = false;
    QString diagnostic;
};

struct DifferentialSample {
    std::uint64_t sequence = 0;
    double timestampSec = 0.0;
    double longitudinalPx = 0.0;
    double transversePx = 0.0;
    double longitudinalArcsec = 0.0;
    double transverseArcsec = 0.0;
    bool valid = false;
};

struct AtmosphereResult {
    bool valid = false;
    bool underResolved = false;
    QString statusMessage;
    std::uint64_t windowEndSequence = 0;
    int validSampleCount = 0;
    double r0LongitudinalM = 0.0;
    double r0TransverseM = 0.0;
    double r0LineOfSightM = 0.0;
    double r0ZenithM = 0.0;
    double seeingArcsec = 0.0;
    double theta0Arcsec = 0.0;
    double tau0Ms = 0.0;
    bool tau0Valid = false;
    double tau0ResolutionMs = 0.0;
    double measuredRateHz = 0.0;
    double longitudinalVarianceArcsec2 = 0.0;
    double transverseVarianceArcsec2 = 0.0;
};

struct MeasurementResult {
    std::uint64_t sequence = 0;
    double timestampSec = 0.0;
    TwoStarMeasurement stars;
    DifferentialSample differential;
    AtmosphereResult atmosphere;
};

struct RoiOverlay {
    RoiRect hardwareAoi;
    RoiRect roiA;
    RoiRect roiB;
    QPointF starA;
    QPointF starB;
    bool hasHardwareAoi = false;
    bool hasRois = false;
};

Q_DECLARE_METATYPE(RoiOverlay)
Q_DECLARE_METATYPE(MeasurementResult)
