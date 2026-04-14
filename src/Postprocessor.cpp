#include "Postprocessor.h"
#include <algorithm>
#include <cmath>
#include <limits>

Postprocessor::Postprocessor(float fps) : fps_(fps) {}

std::vector<float> Postprocessor::maxPool1d(const std::vector<float>& input, int kernelSize)
{
    std::vector<float> output(input.size());
    int half = kernelSize / 2;

    for (size_t i = 0; i < input.size(); ++i)
    {
        float maxVal = -std::numeric_limits<float>::infinity();
        for (int k = -half; k <= half; ++k)
        {
            int idx = static_cast<int>(i) + k;
            if (idx >= 0 && idx < static_cast<int>(input.size()))
                maxVal = std::max(maxVal, input[idx]);
        }
        output[i] = maxVal;
    }

    return output;
}

std::vector<int> Postprocessor::detectPeaks(const std::vector<float>& logits)
{
    // FIX: max_pool applied to this channel only (not concatenated)
    auto pooled = maxPool1d(logits, kMaxPoolKernel);

    std::vector<int> peaks;
    for (size_t i = 0; i < logits.size(); ++i)
    {
        if (logits[i] == pooled[i] && logits[i] > 0.0f)
            peaks.push_back(static_cast<int>(i));
    }

    return peaks;
}

std::vector<int> Postprocessor::deduplicatePeaks(const std::vector<int>& peaks, int width)
{
    if (peaks.empty())
        return {};

    std::vector<int> result;
    double p = peaks[0];
    int c = 1;

    for (size_t i = 1; i < peaks.size(); ++i)
    {
        int p2 = peaks[i];
        if (p2 - p <= width)
        {
            c += 1;
            p += (static_cast<double>(p2) - p) / c;  // running mean
        }
        else
        {
            result.push_back(static_cast<int>(std::round(p)));
            p = p2;
            c = 1;
        }
    }
    result.push_back(static_cast<int>(std::round(p)));

    return result;
}

PostprocessResult Postprocessor::process(const std::vector<float>& beatLogits,
                                          const std::vector<float>& downbeatLogits)
{
    PostprocessResult result;
    result.beatLogits = beatLogits;
    result.downbeatLogits = downbeatLogits;

    // FIX: Detect peaks SEPARATELY for beat and downbeat logits
    // (original beat_this_cpp concatenated them, causing cross-boundary max_pool interference)
    auto beatFrames = detectPeaks(beatLogits);
    auto downbeatFrames = detectPeaks(downbeatLogits);

    // Deduplicate
    beatFrames = deduplicatePeaks(beatFrames, kDedupWidth);
    downbeatFrames = deduplicatePeaks(downbeatFrames, kDedupWidth);

    // Convert frames to seconds
    result.beatTimes.resize(beatFrames.size());
    for (size_t i = 0; i < beatFrames.size(); ++i)
        result.beatTimes[i] = static_cast<float>(beatFrames[i]) / fps_;

    result.downbeatTimes.resize(downbeatFrames.size());
    for (size_t i = 0; i < downbeatFrames.size(); ++i)
        result.downbeatTimes[i] = static_cast<float>(downbeatFrames[i]) / fps_;

    // Snap each downbeat to nearest beat
    if (!result.beatTimes.empty())
    {
        for (size_t i = 0; i < result.downbeatTimes.size(); ++i)
        {
            float dTime = result.downbeatTimes[i];
            float minDiff = std::numeric_limits<float>::max();
            int bestIdx = -1;

            for (size_t j = 0; j < result.beatTimes.size(); ++j)
            {
                float diff = std::abs(result.beatTimes[j] - dTime);
                if (diff < minDiff)
                {
                    minDiff = diff;
                    bestIdx = static_cast<int>(j);
                }
            }

            if (bestIdx >= 0)
                result.downbeatTimes[i] = result.beatTimes[bestIdx];
        }

        // Remove duplicate downbeats after snapping
        std::sort(result.downbeatTimes.begin(), result.downbeatTimes.end());
        result.downbeatTimes.erase(
            std::unique(result.downbeatTimes.begin(), result.downbeatTimes.end()),
            result.downbeatTimes.end());
    }

    return result;
}
