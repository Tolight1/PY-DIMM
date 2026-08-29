#pragma once

#include "CameraTypes.h"

#include <QMetaType>
#include <QPointF>
#include <QString>
#include <QVector>

#include <cstdint>

inline constexpr int kAoiTransitionDiagnosticFrameCount = 50;
inline constexpr int kDifferentialBaselineWindowFrames = 20;

struct OpticalConfig {
    double mainTelescopeApertureMm = 254.0;
    double subApertureDiameterMm = 80.0;
    double baselineSeparationMm = 170.0;
    double baselineAngleDeg = 0.0;
    double focalLengthMm = 2500.0;
    double wavelengthNm = 550.0;
    double pixelSizeUm = 5.86;
    double zenithAngleDeg = 49.6;
};

struct AcquisitionConfig {
    int frameWidth = 1920;
    int frameHeight = 1200;
    PixelFormat pixelFormat = PixelFormat::Mono8;
    double measurementRateHz = 100.0;
    double previewRateHz = 20.0;
    double exposureTimeMs = 5.0;
    int targetSampleCount = 2000;
    double targetDurationSec = 20.0;
    bool enableHardwareAoi = true;
};

struct ProcessingConfig {
    int hardwareAoiMarginPx = 32;
    // Performance targets only.  The tracker never clips an enclosing AOI to
    // these values because doing so could cut off a star; it reports an
    // over-sized request instead.
    int hardwareAoiMaxWidthPx = 300;
    int hardwareAoiMaxHeightPx = 300;
    // Hardware AOI is updated only when a dynamic centroid window is this
    // close to the current hardware-AOI boundary, and passes the independent
    // movement/cooldown guards below.
    int hardwareAoiUpdateDistanceToEdgePx = 32;
    double hardwareAoiUpdateMinimumShiftPx = 16.0;
    int hardwareAoiUpdateCooldownMs = 1000;

    double otsuSigmaThreshold = 4.0;
    double otsuPeakFraction = 0.20;
    int connectivity = 8;
    int otsuMinimumComponentAreaPx = 9;
    int otsuMaximumComponentAreaPx = 1000;
    // UI_2-compatible final centroid kernel: radius 3 means a 7 × 7 kernel.
    int centroidKernelRadiusPx = 3;
    double minimumPeakDistancePx = 8.0;
    double minimumCentroidIntensity = 1.0;
    // Reject an otherwise valid pair when its separation vector jumps by more
    // than this amount from the last accepted pair.  This protects the
    // rolling DIMM window from wrong-component associations near AOI changes.
    double maximumDifferentialJumpPx = 10.0;
    // A short-window baseline catches gradual differential drift. The same
    // pixel threshold is used, but rejection starts only after this many
    // consecutive baseline violations.
    int differentialBaselineViolationFrames = 5;

    int lostPairRelocalizationFrames = 10;

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
    bool drawHardwareAoi = true;
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
    std::uint64_t configurationGeneration = 0;
    std::uint64_t aoiEpoch = 0;
    std::uint64_t transitionId = 0;
    QRect sourceRect;
    TwoStarMeasurement stars;
    DifferentialSample differential;
    AtmosphereResult atmosphere;
};

struct AoiEvent {
    QString type;
    std::uint64_t aoiEpoch = 0;
    std::uint64_t transitionId = 0;
    std::uint64_t frameSequence = 0;
    double frameTimestampSec = 0.0;
    std::uint64_t frameGeneration = 0;
    std::uint64_t previousGeneration = 0;
    std::uint64_t requestedGeneration = 0;
    std::uint64_t appliedGeneration = 0;
    std::uint64_t staleFramesDropped = 0;
    RoiRect requestedAoi;
    RoiRect appliedAoi;
    QPointF starA;
    QPointF starB;
    double longitudinalPx = 0.0;
    double transversePx = 0.0;
    double differentialJumpPx = 0.0;
    double differentialBaselineDeviationPx = 0.0;
    int differentialBaselineViolationFrames = 0;
    bool validPair = false;
    bool sampleAccepted = false;
    std::uint64_t validSampleCount = 0;
    QString reason;
};

struct DisplayOverlay {
    RoiRect hardwareAoi;
    QPointF starA;
    QPointF starB;
    bool hasHardwareAoi = false;
    bool hasCentroids = false;
};

struct AoiTransitionSample {
    std::uint64_t aoiEpoch = 0;
    std::uint64_t transitionId = 0;
    int frameIndexAfterApply = 0;
    std::uint64_t frameSequence = 0;
    double frameTimestampSec = 0.0;
    std::uint64_t frameGeneration = 0;
    QRect sourceRect;
    RoiRect appliedAoi;
    QPointF starA;
    QPointF starB;
    double starAPeakIntensity = 0.0;
    double starBPeakIntensity = 0.0;
    int starAComponentAreaPx = 0;
    int starBComponentAreaPx = 0;
    double longitudinalPx = 0.0;
    double transversePx = 0.0;
    double differentialJumpPx = 0.0;
    double differentialBaselineDeviationPx = 0.0;
    int differentialBaselineViolationFrames = 0;
    bool starAValid = false;
    bool starBValid = false;
    bool validPair = false;
    bool sampleAccepted = false;
    bool differentialContinuityRejected = false;
    bool requestFullFrameRelocalization = false;
    QString diagnostic;
};

Q_DECLARE_METATYPE(DisplayOverlay)
Q_DECLARE_METATYPE(MeasurementResult)
Q_DECLARE_METATYPE(AoiEvent)
Q_DECLARE_METATYPE(AoiTransitionSample)
