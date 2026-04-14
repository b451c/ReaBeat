#include "TimeSigDetector.h"
#include <unordered_map>
#include <algorithm>

int TimeSigDetector::detect(const std::vector<float>& beats,
                             const std::vector<float>& downbeats)
{
    if (downbeats.size() < 2 || beats.size() < 2)
        return 4;

    // Count beats in each bar (between consecutive downbeats)
    std::unordered_map<int, int> counts;

    for (size_t i = 0; i < downbeats.size() - 1; ++i)
    {
        float barStart = downbeats[i] - kTolerance;
        float barEnd = downbeats[i + 1] - kTolerance;

        int n = 0;
        for (float beat : beats)
        {
            if (beat >= barStart && beat < barEnd)
                ++n;
        }

        if (n >= 2 && n <= 7)
            counts[n]++;
    }

    if (counts.empty())
        return 4;

    // Return most common beat count
    int bestCount = 4;
    int bestFreq = 0;
    for (auto& [count, freq] : counts)
    {
        if (freq > bestFreq)
        {
            bestFreq = freq;
            bestCount = count;
        }
    }

    return bestCount;
}
