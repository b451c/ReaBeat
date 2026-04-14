#include "DownbeatCleaner.h"
#include <algorithm>
#include <cmath>

std::vector<float> DownbeatCleaner::clean(const std::vector<float>& rawDownbeats,
                                           float tempo,
                                           int timeSigNum)
{
    if (rawDownbeats.size() < 2)
        return {rawDownbeats.begin(), rawDownbeats.end()};

    // Expected bar duration: e.g. 4/4 at 120 BPM = 60*4/120 = 2.0s
    float expectedBar = 60.0f * static_cast<float>(timeSigNum) / tempo;

    // Compute median interval of raw downbeats
    std::vector<float> intervals;
    intervals.reserve(rawDownbeats.size() - 1);
    for (size_t i = 1; i < rawDownbeats.size(); ++i)
        intervals.push_back(rawDownbeats[i] - rawDownbeats[i - 1]);

    auto sorted = intervals;
    std::sort(sorted.begin(), sorted.end());
    float medianInterval = sorted[sorted.size() / 2];

    // Choose reference interval
    float refInterval;
    if (std::abs(medianInterval - expectedBar) / expectedBar < kRefTolerance)
        refInterval = expectedBar;  // median close to expected - use expected
    else
        refInterval = medianInterval;  // use measured median

    // Filter and fill gaps
    std::vector<float> cleaned;
    cleaned.push_back(rawDownbeats[0]);  // always keep first

    for (size_t i = 1; i < rawDownbeats.size(); ++i)
    {
        float gap = rawDownbeats[i] - cleaned.back();
        float ratio = gap / refInterval;

        if (ratio < kTooCloseRatio)
        {
            // Too close - erroneous extra, skip
            continue;
        }
        else if (ratio > kTooFarRatio)
        {
            // Too far - fill in missing downbeats
            int nMissing = static_cast<int>(std::round(ratio)) - 1;
            float lastDb = cleaned.back();
            for (int j = 1; j <= nMissing; ++j)
            {
                cleaned.push_back(lastDb + refInterval * static_cast<float>(j));
            }
            cleaned.push_back(rawDownbeats[i]);
        }
        else
        {
            // Normal range - keep
            cleaned.push_back(rawDownbeats[i]);
        }
    }

    return cleaned;
}
