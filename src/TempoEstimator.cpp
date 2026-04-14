#include "TempoEstimator.h"
#include <algorithm>
#include <numeric>
#include <cmath>

static constexpr float kPi = 3.14159265358979323846f;

float TempoEstimator::octaveCorrect(float bpm)
{
    if (bpm <= 0.0f)
        return kFallbackBpm;

    while (bpm < kMinBpm)
        bpm *= 2.0f;
    while (bpm > kMaxBpm)
        bpm /= 2.0f;

    return bpm;
}

float TempoEstimator::compute(const std::vector<float>& beats)
{
    if (beats.size() < 2)
        return kFallbackBpm;

    // Trim 15% from edges (avoid intro/outro drift)
    auto n = beats.size();
    auto trim = static_cast<size_t>(n * kEdgeTrimFraction);
    size_t start = trim;
    size_t end = n - trim;

    // If trimmed range too small, use all beats
    if (end - start < 4)
    {
        start = 0;
        end = n;
    }

    // Compute intervals
    std::vector<float> intervals;
    intervals.reserve(end - start);
    for (size_t i = start; i < end - 1; ++i)
    {
        float dt = beats[i + 1] - beats[i];
        if (dt > kIntervalMinSec && dt < kIntervalMaxSec)
            intervals.push_back(dt);
    }

    if (intervals.empty())
        return kFallbackBpm;

    // Median interval
    auto sorted = intervals;
    std::sort(sorted.begin(), sorted.end());
    float median = sorted[sorted.size() / 2];

    // Filter to +/-15% of median
    std::vector<float> valid;
    valid.reserve(intervals.size());
    for (float dt : intervals)
    {
        if (std::abs(dt - median) < median * kMedianFilterTolerance)
            valid.push_back(dt);
    }

    float avgInterval = valid.empty() ? median
        : std::accumulate(valid.begin(), valid.end(), 0.0f) / static_cast<float>(valid.size());

    if (avgInterval <= 0.0f)
        return kFallbackBpm;

    // Phase optimization via circular mean
    // Map each beat to a phase angle within the beat grid
    std::vector<float> subset(beats.begin() + static_cast<long>(start),
                              beats.begin() + static_cast<long>(end));

    float sumSin = 0.0f, sumCos = 0.0f;
    for (float t : subset)
    {
        float phase = std::fmod(t, avgInterval);
        float theta = (phase / avgInterval) * 2.0f * kPi;
        sumSin += std::sin(theta);
        sumCos += std::cos(theta);
    }

    float meanTheta = std::atan2(sumSin, sumCos);
    if (meanTheta < 0.0f)
        meanTheta += 2.0f * kPi;

    float optimalPhase = (meanTheta / (2.0f * kPi)) * avgInterval;

    // Linear regression: beat_indices vs beat_times
    // Map beats to grid indices
    auto subsetN = static_cast<float>(subset.size());
    std::vector<float> indices(subset.size());
    for (size_t i = 0; i < subset.size(); ++i)
        indices[i] = std::round((subset[i] - optimalPhase) / avgInterval);

    // Regression: y = slope * x + intercept
    // slope = (n*sum(xy) - sum(x)*sum(y)) / (n*sum(xx) - sum(x)^2)
    float sumX = 0, sumY = 0, sumXY = 0, sumXX = 0;
    for (size_t i = 0; i < subset.size(); ++i)
    {
        float x = indices[i];
        float y = subset[i];
        sumX += x;
        sumY += y;
        sumXY += x * y;
        sumXX += x * x;
    }

    float denom = subsetN * sumXX - sumX * sumX;
    if (std::abs(denom) < 1e-10f)
        return octaveCorrect(60.0f / avgInterval);

    float slope = (subsetN * sumXY - sumX * sumY) / denom;

    // Compute r-squared
    float meanY = sumY / subsetN;
    float ssTot = 0, ssRes = 0;
    float intercept = (sumY - slope * sumX) / subsetN;
    for (size_t i = 0; i < subset.size(); ++i)
    {
        float predicted = slope * indices[i] + intercept;
        float residual = subset[i] - predicted;
        ssRes += residual * residual;
        float diff = subset[i] - meanY;
        ssTot += diff * diff;
    }

    float rSquared = (ssTot > 0) ? (1.0f - ssRes / ssTot) : 0.0f;

    if (slope > 0.0f && rSquared > kRSquaredThreshold)
        return octaveCorrect(60.0f / slope);

    // Fallback: mean-based BPM
    return octaveCorrect(60.0f / avgInterval);
}
