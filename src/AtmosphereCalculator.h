#pragma once

#include "AppConfig.h"

#include <deque>

// DIMM-compatible four-parameter calculation. All physics formulas and unit
// conversions live here; no formula appears in MainWindow, MeasurementWorker,
// or the settings dialog.
class AtmosphereCalculator final {
public:
    explicit AtmosphereCalculator(AppConfig config);

    void setConfig(AppConfig config);
    void reset();
    void append(DifferentialSample sample);
    AtmosphereResult calculate(std::uint64_t windowEndSequence,
                               double measuredRateHz) const;
    int validSampleCount() const;

    // Projects a validated two-star measurement into a differential sample.
    // Keeps the pixel-to-arcsec projection inside the calculator so no formula
    // is duplicated in the worker.
    static DifferentialSample makeSample(const TwoStarMeasurement &stars,
                                         const OpticalConfig &optical,
                                         double timestampSec);

private:
    double calculateTau0(bool &underResolved,
                         bool &valid,
                         double &resolutionMs) const;

    AppConfig config_;
    std::deque<DifferentialSample> window_;
    std::deque<DifferentialSample> tauHistory_;
};
