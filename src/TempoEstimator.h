#pragma once
#include <vector>
#include <cmath>

// Phase-aware BPM estimation with octave correction.
// Port of detector.py _compute_tempo() + _octave_correct()

class TempoEstimator
{
public:
    // Compute BPM from beat positions (seconds).
    // Returns BPM in 78-185 range (octave-corrected).
    static float compute(const std::vector<float>& beats);

    // Octave-correct a BPM value into 78-185 range.
    static float octaveCorrect(float bpm);

private:
    static constexpr float kMinBpm = 78.0f;
    static constexpr float kMaxBpm = 185.0f;
    static constexpr float kFallbackBpm = 120.0f;
    static constexpr float kEdgeTrimFraction = 0.15f;
    static constexpr float kIntervalMinSec = 0.2f;
    static constexpr float kIntervalMaxSec = 2.0f;
    static constexpr float kMedianFilterTolerance = 0.15f;
    static constexpr float kRSquaredThreshold = 0.99f;
};
