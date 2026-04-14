#pragma once
#include <vector>

// Snap beat positions to nearest audio transient for sample-level precision.
// Replaces Python librosa.onset.onset_strength + onset_detect.
// Uses spectral flux for onset detection (core of librosa's approach).

class OnsetRefinement
{
public:
    // Refine beat/downbeat positions to nearest transient.
    // audio: mono audio at given sample rate
    // positions: beat times in seconds to refine
    // windowSec: search window around each beat (+/- seconds)
    static std::vector<float> refine(const std::vector<float>& audio,
                                      int sampleRate,
                                      const std::vector<float>& positions,
                                      float windowSec = 0.030f);

private:
    // Detect onset times from audio using spectral flux
    static std::vector<float> detectOnsets(const std::vector<float>& audio,
                                            int sampleRate);

    static constexpr int kHop = 64;         // ~3ms per frame at 22050 Hz
    static constexpr int kNfft = 1024;      // FFT window size
    static constexpr int kAdaptiveWindow = 7; // frames for adaptive threshold
    static constexpr float kDelta = 0.3f;   // threshold = mean + delta * std
};
