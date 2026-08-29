#include "AtmosphereCalculator.h"

#include <QtGlobal>

#include <algorithm>
#include <cmath>
#include <numeric>
#include <vector>

namespace {

constexpr double kPi = 3.14159265358979323846264338327950288;
constexpr double kArcsecToRad = kPi / (180.0 * 3600.0);

// Population variance with denominator N, exactly as in the DIMM reference.
double populationVariance(const std::vector<double> &values)
{
    if (values.empty())
        return 0.0;
    const double mean = std::accumulate(values.begin(), values.end(), 0.0) /
                        static_cast<double>(values.size());
    double sum = 0.0;
    for (double value : values)
        sum += (value - mean) * (value - mean);
    return sum / static_cast<double>(values.size());
}

// Normalized autocorrelation series crossing of 1/e, linearly interpolated
// between the two adjacent lag points. Returns the crossing time in seconds.
struct TauEstimate {
    bool valid = false;
    bool underResolved = false;
    double valueSec = 0.0;
    double resolutionSec = 0.0;
};

TauEstimate first1eCrossing(const std::vector<double> &values,
                            const std::vector<double> &timestamps,
                            double maxLagSec)
{
    TauEstimate estimate;
    const std::size_t n = values.size();
    if (n < 2)
        return estimate;

    const double mean = std::accumulate(values.begin(), values.end(), 0.0) /
                        static_cast<double>(n);
    double denominator = 0.0;
    for (double v : values)
        denominator += (v - mean) * (v - mean);
    if (denominator <= 0.0)
        return estimate;

    std::vector<double> intervals;
    intervals.reserve(timestamps.size() - 1);
    for (std::size_t i = 1; i < timestamps.size(); ++i) {
        const double interval = timestamps[i] - timestamps[i - 1];
        if (interval > 0.0)
            intervals.push_back(interval);
    }
    if (intervals.empty())
        return estimate;
    std::sort(intervals.begin(), intervals.end());
    const double sampleIntervalSec =
        intervals[intervals.size() / 2];
    if (sampleIntervalSec <= 0.0)
        return estimate;

    const double target = 1.0 / std::exp(1.0);
    const std::size_t maxLag = std::min(
        n / 2,
        static_cast<std::size_t>(std::max(
            1.0, std::ceil(maxLagSec / sampleIntervalSec))));
    double previousAc = 1.0;
    for (std::size_t k = 1; k <= maxLag; ++k) {
        const double lag = static_cast<double>(k) * sampleIntervalSec;
        double sum = 0.0;
        for (std::size_t i = 0; i + k < n; ++i)
            sum += (values[i] - mean) * (values[i + k] - mean);
        // Same normalization as DIMM: the lagged numerator uses N-lag,
        // while the variance denominator uses N.
        const double ac =
            (sum / static_cast<double>(n - k)) /
            (denominator / static_cast<double>(n));
        if (ac <= target) {
            estimate.valid = true;
            estimate.resolutionSec = sampleIntervalSec;
            // DIMM treats a crossing at lag 1 as under-resolved and reports
            // the sampling interval as the resolution limit.
            if (k == 1) {
                estimate.underResolved = true;
                estimate.valueSec = sampleIntervalSec;
                return estimate;
            }
            const double denominatorAc = ac - previousAc;
            const double fraction = std::abs(denominatorAc) > 1.0e-12
                ? (target - previousAc) / denominatorAc
                : 0.0;
            estimate.valueSec =
                (static_cast<double>(k - 1) + fraction) * sampleIntervalSec;
            return estimate;
        }
        previousAc = ac;
    }
    return estimate;
}

} // namespace

AtmosphereCalculator::AtmosphereCalculator(AppConfig config)
    : config_(config)
{
}

void AtmosphereCalculator::setConfig(AppConfig config)
{
    config_ = config;
}

void AtmosphereCalculator::reset()
{
    window_.clear();
    tauHistory_.clear();
}

void AtmosphereCalculator::append(DifferentialSample sample)
{
    if (!sample.valid)
        return;

    // Frame-count-based rolling window: only the latest r0WindowFrames samples.
    window_.push_back(sample);
    while (window_.size() >
           static_cast<std::size_t>(config_.processing.r0WindowFrames))
        window_.pop_front();

    // A dropped frame breaks the continuous autocorrelation series. Keep the
    // newest continuous segment, matching the DIMM reference behavior.
    if (!tauHistory_.empty() && sample.sequence != 0 &&
        sample.sequence != tauHistory_.back().sequence + 1) {
        tauHistory_.clear();
    }

    // tau0 history: latest tau0HistorySeconds of samples by timestamp.
    tauHistory_.push_back(sample);
    while (tauHistory_.size() >= 2 &&
           tauHistory_.back().timestampSec - tauHistory_.front().timestampSec >
               static_cast<double>(config_.processing.tau0HistorySeconds))
        tauHistory_.pop_front();
}

int AtmosphereCalculator::validSampleCount() const
{
    return static_cast<int>(window_.size());
}

AtmosphereResult AtmosphereCalculator::calculate(std::uint64_t windowEndSequence,
                                                 double measuredRateHz) const
{
    AtmosphereResult result;
    result.windowEndSequence = windowEndSequence;
    result.measuredRateHz = measuredRateHz;
    result.valid = false;
    const std::size_t n = window_.size();
    result.validSampleCount = static_cast<int>(n);


    if (n < static_cast<std::size_t>(config_.processing.r0WindowFrames)) {
        result.statusMessage = QStringLiteral("等待足够的有效样本（%1 / %2）")
                                   .arg(result.validSampleCount)
                                   .arg(config_.processing.r0WindowFrames);
        return result;
    }

    std::vector<double> longitudinalRad;
    std::vector<double> transverseRad;
    std::vector<double> longitudinalArcsec;
    std::vector<double> transverseArcsec;
    longitudinalRad.reserve(n);
    transverseRad.reserve(n);
    longitudinalArcsec.reserve(n);
    transverseArcsec.reserve(n);
    for (const auto &s : window_) {
        longitudinalRad.push_back(s.longitudinalArcsec * kArcsecToRad);
        transverseRad.push_back(s.transverseArcsec * kArcsecToRad);
        longitudinalArcsec.push_back(s.longitudinalArcsec);
        transverseArcsec.push_back(s.transverseArcsec);
    }

    const double longitudinalVarianceRad2 = populationVariance(longitudinalRad);
    const double transverseVarianceRad2 = populationVariance(transverseRad);
    result.longitudinalVarianceArcsec2 = populationVariance(longitudinalArcsec);
    result.transverseVarianceArcsec2 = populationVariance(transverseArcsec);

    if (longitudinalVarianceRad2 <= 0.0 || transverseVarianceRad2 <= 0.0) {
        result.statusMessage = QStringLiteral("差分方差为零，无法计算 r0");
        return result;
    }

    const double lambdaM = config_.optical.wavelengthNm * 1.0e-9;
    const double diameterM = config_.optical.subApertureDiameterMm * 1.0e-3;
    const double baselineM = config_.optical.baselineSeparationMm * 1.0e-3;

    // DIMM coefficients. The physical baseline is the distance between the two
    // telescope circular-window centers (150 mm default), not an image crop
    // distance and not a prism-equivalent baseline.
    const double coefficientLongitudinal = 2.0 * lambdaM * lambdaM *
        (0.179 * std::pow(diameterM, -1.0 / 3.0) -
         0.0968 * std::pow(baselineM, -1.0 / 3.0));
    const double coefficientTransverse = 2.0 * lambdaM * lambdaM *
        (0.179 * std::pow(diameterM, -1.0 / 3.0) -
         0.145 * std::pow(baselineM, -1.0 / 3.0));

    const double r0Longitudinal = std::pow(
        coefficientLongitudinal / longitudinalVarianceRad2, 3.0 / 5.0);
    const double r0Transverse = std::pow(
        coefficientTransverse / transverseVarianceRad2, 3.0 / 5.0);
    const double r0LineOfSight = 0.5 * (r0Longitudinal + r0Transverse);

    const double zenithRad = config_.optical.zenithAngleDeg * kPi / 180.0;
    const double r0Zenith = r0LineOfSight * std::pow(std::cos(zenithRad), -3.0 / 5.0);
    const double seeingArcsec = 0.98 * lambdaM / r0Zenith * 206265.0;

    const double theta0Arcsec = 0.64 *
        (4.0 / std::pow(result.longitudinalVarianceArcsec2, 0.65)) *
        std::pow(std::cos(zenithRad), 8.0 / 5.0);

    bool underResolved = false;
    bool tau0Valid = false;
    double tau0ResolutionMs = 0.0;
    const double tau0Ms = calculateTau0(underResolved, tau0Valid,
                                        tau0ResolutionMs);

    result.r0LongitudinalM = r0Longitudinal;
    result.r0TransverseM = r0Transverse;
    result.r0LineOfSightM = r0LineOfSight;
    result.r0ZenithM = r0Zenith;
    result.seeingArcsec = seeingArcsec;
    result.theta0Arcsec = theta0Arcsec;
    result.tau0Ms = tau0Ms;
    result.tau0Valid = tau0Valid;
    result.tau0ResolutionMs = tau0ResolutionMs;
    result.underResolved = underResolved;
    result.valid = true;
    result.statusMessage = QStringLiteral("有效");
    return result;
}

double AtmosphereCalculator::calculateTau0(bool &underResolved,
                                           bool &valid,
                                           double &resolutionMs) const
{
    underResolved = false;
    valid = false;
    resolutionMs = 0.0;

    const int minimumSamples = config_.processing.tau0MinimumSamples;
    if (static_cast<int>(tauHistory_.size()) < minimumSamples) {
        underResolved = true;
        return 0.0;
    }

    const double maxLagSec = config_.processing.tau0MaximumLagMs * 1.0e-3;

    std::vector<double> longitudinal;
    std::vector<double> transverse;
    std::vector<double> timestamps;
    longitudinal.reserve(tauHistory_.size());
    transverse.reserve(tauHistory_.size());
    timestamps.reserve(tauHistory_.size());
    for (const auto &s : tauHistory_) {
        longitudinal.push_back(s.longitudinalArcsec * kArcsecToRad);
        transverse.push_back(s.transverseArcsec * kArcsecToRad);
        timestamps.push_back(s.timestampSec);
    }

    const TauEstimate longitudinalEstimate =
        first1eCrossing(longitudinal, timestamps, maxLagSec);
    const TauEstimate transverseEstimate =
        first1eCrossing(transverse, timestamps, maxLagSec);

    // DIMM combination rule: average two resolved directions; if either
    // direction is only one-sample resolved, report the common resolution
    // limit and preserve the under-resolved flag.
    if (longitudinalEstimate.valid && transverseEstimate.valid) {
        if (longitudinalEstimate.underResolved ||
            transverseEstimate.underResolved) {
            underResolved = true;
            valid = true;
            resolutionMs = std::max(longitudinalEstimate.resolutionSec,
                                    transverseEstimate.resolutionSec) * 1000.0;
            return std::max(longitudinalEstimate.resolutionSec,
                            transverseEstimate.resolutionSec) * 1000.0;
        }
        valid = true;
        resolutionMs = 0.5 * (longitudinalEstimate.resolutionSec +
                              transverseEstimate.resolutionSec) * 1000.0;
        return 0.5 * (longitudinalEstimate.valueSec +
                      transverseEstimate.valueSec) * 1000.0;
    }
    if (longitudinalEstimate.valid) {
        underResolved = longitudinalEstimate.underResolved;
        valid = true;
        resolutionMs = longitudinalEstimate.resolutionSec * 1000.0;
        return longitudinalEstimate.valueSec * 1000.0;
    }
    if (transverseEstimate.valid) {
        underResolved = transverseEstimate.underResolved;
        valid = true;
        resolutionMs = transverseEstimate.resolutionSec * 1000.0;
        return transverseEstimate.valueSec * 1000.0;
    }

    underResolved = true;
    return 0.0;
}

DifferentialSample AtmosphereCalculator::makeSample(const TwoStarMeasurement &stars,
                                                    const OpticalConfig &optical,
                                                    double timestampSec)
{
    DifferentialSample sample;
    sample.timestampSec = timestampSec;
    sample.valid = false;
    if (!stars.validPair)
        return sample;

    // Image-space differential in the rotated longitudinal/transverse frame.
    // This is the same baseline-angle projection used by the UI_2 reference.
    const double angleRad = optical.baselineAngleDeg * kPi / 180.0;
    const double dx = stars.fullFrameStarB.x() - stars.fullFrameStarA.x();
    const double dy = stars.fullFrameStarB.y() - stars.fullFrameStarA.y();
    const double longitudinalPx = dx * std::cos(angleRad) + dy * std::sin(angleRad);
    const double transversePx = -dx * std::sin(angleRad) + dy * std::cos(angleRad);

    const double pixelScaleRad =
        (optical.pixelSizeUm * 1.0e-6) /
        (optical.focalLengthMm * 1.0e-3);
    const double arcsecPerPixel = pixelScaleRad * 206265.0;

    sample.longitudinalPx = longitudinalPx;
    sample.transversePx = transversePx;
    sample.longitudinalArcsec = longitudinalPx * arcsecPerPixel;
    sample.transverseArcsec = transversePx * arcsecPerPixel;
    sample.valid = true;
    return sample;
}
