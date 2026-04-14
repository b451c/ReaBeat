#include "MelSpectrogram.h"
#include "pocketfft_hdronly.h"
#include <algorithm>
#include <numbers>

MelSpectrogram::MelSpectrogram()
{
    createFilterbank();
    createWindow();
}

float MelSpectrogram::hzToMel(float hz)
{
    // Slaney scale (matches librosa/torchaudio)
    constexpr double fMin = 0.0;
    constexpr double fSp = 200.0 / 3.0;
    double mels = (hz - fMin) / fSp;

    constexpr double minLogHz = 1000.0;
    constexpr double minLogMel = (minLogHz - fMin) / fSp;
    const double logstep = std::log(6.4) / 27.0;

    if (hz >= minLogHz)
        mels = minLogMel + std::log(hz / minLogHz) / logstep;

    return static_cast<float>(mels);
}

float MelSpectrogram::melToHz(float mel)
{
    constexpr double fMin = 0.0;
    constexpr double fSp = 200.0 / 3.0;
    double freqs = fMin + fSp * mel;

    constexpr double minLogHz = 1000.0;
    constexpr double minLogMel = (minLogHz - fMin) / fSp;
    const double logstep = std::log(6.4) / 27.0;

    if (mel >= minLogMel)
        freqs = minLogHz * std::exp(logstep * (mel - minLogMel));

    return static_cast<float>(freqs);
}

void MelSpectrogram::createWindow()
{
    window_.resize(kWinLength);
    for (int i = 0; i < kWinLength; ++i)
        window_[i] = 0.5f * (1.0f - std::cos(2.0f * static_cast<float>(std::numbers::pi) * i / kWinLength));
}

void MelSpectrogram::createFilterbank()
{
    constexpr int nFreqs = kNfft / 2 + 1;

    double melMin = hzToMel(kFmin);
    double melMax = hzToMel(kFmax);

    // n_mels + 2 evenly spaced mel points
    std::vector<double> melPoints(kNmels + 2);
    for (int i = 0; i < kNmels + 2; ++i)
        melPoints[i] = melMin + (melMax - melMin) * i / (kNmels + 1);

    // Convert to Hz
    std::vector<double> hzPoints(kNmels + 2);
    for (int i = 0; i < kNmels + 2; ++i)
        hzPoints[i] = melToHz(static_cast<float>(melPoints[i]));

    // Frequency of each FFT bin
    std::vector<double> freqs(nFreqs);
    for (int i = 0; i < nFreqs; ++i)
        freqs[i] = static_cast<double>(i) * kSampleRate / kNfft;

    // Build triangular filterbank [nFreqs][kNmels]
    filterbank_.resize(nFreqs, std::vector<double>(kNmels, 0.0));
    for (int i = 0; i < nFreqs; ++i)
    {
        double hzI = freqs[i];
        for (int j = 0; j < kNmels; ++j)
        {
            double hzLeft = hzPoints[j];
            double hzCenter = hzPoints[j + 1];
            double hzRight = hzPoints[j + 2];

            double leftSlope = (hzCenter != hzLeft) ? (hzI - hzLeft) / (hzCenter - hzLeft) : 0.0;
            double rightSlope = (hzRight != hzCenter) ? (hzRight - hzI) / (hzRight - hzCenter) : 0.0;

            filterbank_[i][j] = std::max(0.0, std::min(leftSlope, rightSlope));
        }
    }
}

int MelSpectrogram::getFrameCount(int audioLength) const
{
    int padded = audioLength + kNfft;  // center padding adds n_fft/2 on each side
    return (padded - kNfft) / kHopLength + 1;
}

std::vector<std::vector<float>> MelSpectrogram::compute(const std::vector<float>& audio)
{
    constexpr int nFreqs = kNfft / 2 + 1;
    int padSize = kNfft / 2;

    // Reflect padding (center=True, pad_mode="reflect")
    std::vector<float> padded;
    padded.reserve(audio.size() + 2 * padSize);

    for (int i = padSize; i >= 1; --i)
        padded.push_back(audio[i]);

    padded.insert(padded.end(), audio.begin(), audio.end());

    int audioSize = static_cast<int>(audio.size());
    for (int i = 1; i <= padSize; ++i)
        padded.push_back(audio[audioSize - 1 - i]);

    int numFrames = (static_cast<int>(padded.size()) - kNfft) / kHopLength + 1;
    if (numFrames <= 0)
        return {};

    std::vector<std::vector<float>> output(numFrames, std::vector<float>(kNmels));

    // PocketFFT setup
    pocketfft::shape_t shape = {static_cast<size_t>(kNfft)};
    pocketfft::stride_t strideIn = {sizeof(float)};
    pocketfft::stride_t strideOut = {sizeof(std::complex<float>)};

    std::vector<float> frame(kNfft);
    std::vector<std::complex<float>> fftOut(nFreqs);

    // beat_this uses torchaudio MelSpectrogram with normalized="frame_length",
    // which divides FFT output by sqrt(n_fft) = sqrt(1024) = 32.
    // This is REQUIRED to match the model's training pipeline.
    // Previous code omitted this, causing ~3.4 higher log-mel values and
    // model hallucinating beats everywhere (714 vs 413 on Mark Ronson).
    constexpr double kNormFactor = 1.0 / 32.0;  // 1/sqrt(1024)

    for (int i = 0; i < numFrames; ++i)
    {
        // Extract and window frame
        int start = i * kHopLength;
        for (int j = 0; j < kNfft; ++j)
            frame[j] = padded[start + j] * window_[j];

        // FFT (PocketFFT r2c, unnormalized)
        pocketfft::r2c(shape, strideIn, strideOut, 0, true, frame.data(), fftOut.data(), 1.0f);

        // Amplitude spectrum (magnitude, power=1), normalized by sqrt(n_fft),
        // then mel filterbank + log scaling
        for (int m = 0; m < kNmels; ++m)
        {
            double melEnergy = 0.0;
            for (int k = 0; k < nFreqs; ++k)
            {
                double real = fftOut[k].real();
                double imag = fftOut[k].imag();
                double amplitude = std::sqrt(real * real + imag * imag) * kNormFactor;
                melEnergy += amplitude * filterbank_[k][m];
            }
            output[i][m] = static_cast<float>(std::log1p(kLogMultiplier * std::max(melEnergy, kAmin)));
        }
    }

    return output;
}
