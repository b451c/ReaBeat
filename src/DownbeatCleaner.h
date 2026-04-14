#pragma once
#include <vector>

// Clean neural downbeat positions: remove erroneous extras, fill gaps.
// Port of detector.py _clean_downbeats()

class DownbeatCleaner
{
public:
    // Clean raw neural downbeats.
    // tempo: detected BPM
    // timeSigNum: time signature numerator (3, 4, etc.)
    static std::vector<float> clean(const std::vector<float>& rawDownbeats,
                                     float tempo,
                                     int timeSigNum);

private:
    static constexpr float kTooCloseRatio = 0.6f;   // < 60% of expected = erroneous extra
    static constexpr float kTooFarRatio = 1.35f;    // > 135% of expected = missing
    static constexpr float kRefTolerance = 0.15f;    // +/-15% tolerance for reference interval
};
