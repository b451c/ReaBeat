#include "BeatInterpolator.h"
#include <algorithm>
#include <cmath>
#include <numeric>

std::vector<float> BeatInterpolator::interpolate(
    const std::vector<float>& beats,
    const std::vector<float>& beatLogits,
    float fps)
{
    if (beats.size() < 3)
        return beats;

    // Compute median beat interval
    std::vector<float> intervals;
    intervals.reserve(beats.size() - 1);
    for (size_t i = 1; i < beats.size(); ++i)
        intervals.push_back(beats[i] - beats[i - 1]);

    auto sorted = intervals;
    std::sort(sorted.begin(), sorted.end());
    float median = sorted[sorted.size() / 2];

    if (median <= 0.0f)
        return beats;

    std::vector<float> result;
    result.reserve(beats.size() * 2);  // generous reserve
    result.push_back(beats[0]);

    for (size_t i = 1; i < beats.size(); ++i)
    {
        float gap = beats[i] - beats[i - 1];
        float ratio = gap / median;

        if (ratio <= kGapThreshold)
        {
            // Normal gap - keep beat as-is
            result.push_back(beats[i]);
            continue;
        }

        // Gap is too large - try to fill missing beats
        int nExpected = static_cast<int>(std::round(ratio));
        bool usedLogits = false;

        // Try sub-threshold logit hints if available
        if (!beatLogits.empty() && nExpected > 1)
        {
            // Find sub-threshold peaks in the gap region
            int frameStart = static_cast<int>(beats[i - 1] * fps) + 1;
            int frameEnd = static_cast<int>(beats[i] * fps);
            frameEnd = std::min(frameEnd, static_cast<int>(beatLogits.size()) - 1);

            std::vector<float> hints;
            for (int f = frameStart; f <= frameEnd; ++f)
            {
                if (f >= 0 && f < static_cast<int>(beatLogits.size()))
                {
                    float logit = beatLogits[f];
                    if (logit > kSubThresholdMin && logit < kSubThresholdMax)
                    {
                        // Check if this is a local max (simple peak picking)
                        bool isPeak = true;
                        for (int k = 1; k <= 3; ++k)
                        {
                            if (f - k >= 0 && beatLogits[f - k] > logit) isPeak = false;
                            if (f + k < static_cast<int>(beatLogits.size()) && beatLogits[f + k] > logit) isPeak = false;
                        }
                        if (isPeak)
                            hints.push_back(static_cast<float>(f) / fps);
                    }
                }
            }

            // Check if hints fall at approximately expected positions
            if (static_cast<int>(hints.size()) >= nExpected - 1)
            {
                // Verify hints are roughly evenly spaced
                float expectedSpacing = gap / static_cast<float>(nExpected);
                int used = 0;

                for (float hint : hints)
                {
                    // Check if this hint is near an expected position
                    float relPos = (hint - beats[i - 1]) / gap;
                    bool matchesExpected = false;
                    for (int e = 1; e < nExpected; ++e)
                    {
                        float expectedRel = static_cast<float>(e) / static_cast<float>(nExpected);
                        if (std::abs(relPos - expectedRel) < kPositionTolerance)
                        {
                            matchesExpected = true;
                            break;
                        }
                    }
                    if (matchesExpected)
                        ++used;
                }

                if (used >= nExpected - 1)
                {
                    // Use the best-matching hints — one hint per expected slot
                    std::vector<bool> filled(nExpected, false);
                    for (float hint : hints)
                    {
                        float relPos = (hint - beats[i - 1]) / gap;
                        for (int e = 1; e < nExpected; ++e)
                        {
                            if (filled[e]) continue;
                            float expectedRel = static_cast<float>(e) / static_cast<float>(nExpected);
                            if (std::abs(relPos - expectedRel) < kPositionTolerance)
                            {
                                result.push_back(hint);
                                filled[e] = true;
                                break;
                            }
                        }
                    }
                    usedLogits = true;
                }
            }
        }

        // Fallback: interpolate evenly
        if (!usedLogits && nExpected > 1)
        {
            float spacing = gap / static_cast<float>(nExpected);
            for (int j = 1; j < nExpected; ++j)
                result.push_back(beats[i - 1] + spacing * static_cast<float>(j));
        }

        result.push_back(beats[i]);
    }

    std::sort(result.begin(), result.end());
    return result;
}
