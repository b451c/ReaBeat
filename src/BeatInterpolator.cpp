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

            // Check if hints fall at approximately expected positions.
            // Each expected slot gets its NEAREST unused hint, and acceptance
            // counts DISTINCT slots - the earlier logic counted two hints
            // near one slot twice, accepted the gap, then emitted fewer
            // beats than expected, leaving an under-filled gap flagged as
            // handled (the exact 0.51x-ratio symptom this module fixes).
            if (static_cast<int>(hints.size()) >= nExpected - 1)
            {
                std::vector<int> slotHint(static_cast<size_t>(nExpected), -1);
                std::vector<bool> hintUsed(hints.size(), false);
                int hinted = 0;

                for (int e = 1; e < nExpected; ++e)
                {
                    float expectedRel = static_cast<float>(e) / static_cast<float>(nExpected);
                    int best = -1;
                    float bestDist = kPositionTolerance;
                    for (size_t h = 0; h < hints.size(); ++h)
                    {
                        if (hintUsed[h]) continue;
                        float relPos = (hints[h] - beats[i - 1]) / gap;
                        float d = std::abs(relPos - expectedRel);
                        if (d < bestDist) { bestDist = d; best = static_cast<int>(h); }
                    }
                    if (best >= 0)
                    {
                        slotHint[static_cast<size_t>(e)] = best;
                        hintUsed[static_cast<size_t>(best)] = true;
                        ++hinted;
                    }
                }

                if (hinted >= nExpected - 1)
                {
                    // Every slot has a hint - use them
                    for (int e = 1; e < nExpected; ++e)
                        result.push_back(hints[static_cast<size_t>(slotHint[static_cast<size_t>(e)])]);
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
