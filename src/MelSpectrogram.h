#pragma once
#include <vector>
#include <complex>
#include <cmath>

// Mel-scale spectrogram computation matching Python beat-this / torchaudio.
// Port of beat_this_cpp MelSpectrogram with fixes:
// - Filterbank cached in constructor (not recomputed per call)
// - No debug accessors
// - No miniaudio dependency

class MelSpectrogram
{
public:
    MelSpectrogram();

    // Compute mel spectrogram from mono audio at 22050 Hz.
    // Returns [frames][128] log-mel values.
    std::vector<std::vector<float>> compute(const std::vector<float>& audio);

    // Get frame count for given audio length
    int getFrameCount(int audioLength) const;

private:
    static constexpr int kSampleRate = 22050;
    static constexpr int kNfft = 1024;
    static constexpr int kWinLength = 1024;
    static constexpr int kHopLength = 441;
    static constexpr int kNmels = 128;
    static constexpr int kFmin = 30;
    static constexpr int kFmax = 11000;
    static constexpr float kLogMultiplier = 1000.0f;
    static constexpr double kAmin = 1e-10;

    // Cached mel filterbank [n_fft/2+1][n_mels]
    std::vector<std::vector<double>> filterbank_;
    // Cached hann window
    std::vector<float> window_;

    void createFilterbank();
    void createWindow();
    static float hzToMel(float hz);
    static float melToHz(float mel);
};
