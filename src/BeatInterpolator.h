#pragma once
#include <vector>

// Fill missing beats in gaps where beat-this missed detections.
// Addresses squibs' 0.51x stretch ratio issue (quiet sections).
// NEW feature - not in Python version.

class BeatInterpolator
{
public:
    // Interpolate missing beats in gaps.
    // beats: detected beat positions (seconds)
    // beatLogits: raw model logits per frame (optional, for sub-threshold hints)
    // fps: model frame rate (50.0 for beat-this)
    static std::vector<float> interpolate(
        const std::vector<float>& beats,
        const std::vector<float>& beatLogits = {},
        float fps = 50.0f);

private:
    static constexpr float kGapThreshold = 1.35f;    // gap > 1.35x median = missing beats
    static constexpr float kSubThresholdMin = -2.0f;  // logit > -2.0 = model saw something
    static constexpr float kSubThresholdMax = 0.0f;   // logit < 0.0 = below detection threshold
    static constexpr float kPositionTolerance = 0.20f; // hint within 20% of expected = use it
};
