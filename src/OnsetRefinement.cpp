#include "OnsetRefinement.h"
#include "pocketfft_hdronly.h"
#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>
#include <numeric>

std::vector<float> OnsetRefinement::detectOnsets(const std::vector<float>& audio,
                                                  int sampleRate)
{
    int audioLen = static_cast<int>(audio.size());
    if (audioLen < kNfft)
        return {};

    // Hann window
    std::vector<float> window(kNfft);
    for (int i = 0; i < kNfft; ++i)
        window[i] = 0.5f * (1.0f - std::cos(2.0f * static_cast<float>(std::numbers::pi) * i / kNfft));

    int nFreqs = kNfft / 2 + 1;
    int numFrames = (audioLen - kNfft) / kHop + 1;
    if (numFrames < 2)
        return {};

    // Compute magnitude spectra for all frames
    pocketfft::shape_t shape = {static_cast<size_t>(kNfft)};
    pocketfft::stride_t strideIn = {sizeof(float)};
    pocketfft::stride_t strideOut = {sizeof(std::complex<float>)};

    std::vector<float> frame(kNfft);
    std::vector<std::complex<float>> fftOut(nFreqs);
    std::vector<std::vector<float>> magnitudes(numFrames, std::vector<float>(nFreqs));

    for (int i = 0; i < numFrames; ++i)
    {
        int start = i * kHop;
        for (int j = 0; j < kNfft; ++j)
        {
            int idx = start + j;
            frame[j] = (idx < audioLen) ? audio[idx] * window[j] : 0.0f;
        }

        pocketfft::r2c(shape, strideIn, strideOut, 0, true, frame.data(), fftOut.data(), 1.0f);

        for (int k = 0; k < nFreqs; ++k)
        {
            float r = fftOut[k].real();
            float im = fftOut[k].imag();
            magnitudes[i][k] = std::sqrt(r * r + im * im);
        }
    }

    // Spectral flux: sum of positive magnitude differences between frames
    std::vector<float> flux(numFrames, 0.0f);
    for (int i = 1; i < numFrames; ++i)
    {
        float sum = 0.0f;
        for (int k = 0; k < nFreqs; ++k)
        {
            float diff = magnitudes[i][k] - magnitudes[i - 1][k];
            if (diff > 0.0f)
                sum += diff;
        }
        flux[i] = sum;
    }

    // Peak picking with adaptive threshold
    std::vector<float> onsetTimes;
    int halfWin = kAdaptiveWindow / 2;

    for (int i = 1; i < numFrames - 1; ++i)
    {
        // Must be a local maximum
        if (flux[i] <= flux[i - 1] || flux[i] <= flux[i + 1])
            continue;

        // Adaptive threshold: mean + delta * std in local window
        int wStart = std::max(0, i - halfWin);
        int wEnd = std::min(numFrames, i + halfWin + 1);
        int wLen = wEnd - wStart;

        float mean = 0;
        for (int j = wStart; j < wEnd; ++j)
            mean += flux[j];
        mean /= wLen;

        float variance = 0;
        for (int j = wStart; j < wEnd; ++j)
        {
            float d = flux[j] - mean;
            variance += d * d;
        }
        float stddev = std::sqrt(variance / wLen);

        float threshold = mean + kDelta * stddev;

        if (flux[i] > threshold)
            onsetTimes.push_back(static_cast<float>(i * kHop) / sampleRate);
    }

    return onsetTimes;
}

std::vector<float> OnsetRefinement::refine(const std::vector<float>& audio,
                                            int sampleRate,
                                            const std::vector<float>& positions,
                                            float windowSec)
{
    if (positions.empty())
        return positions;

    auto onsets = detectOnsets(audio, sampleRate);
    if (onsets.empty())
        return positions;

    std::vector<float> refined = positions;

    for (size_t i = 0; i < refined.size(); ++i)
    {
        float pos = refined[i];
        float bestDist = windowSec + 1.0f;
        float bestOnset = pos;

        // Binary search for closest onset (onsets are sorted)
        auto it = std::lower_bound(onsets.begin(), onsets.end(), pos - windowSec);
        while (it != onsets.end() && *it <= pos + windowSec)
        {
            float dist = std::abs(*it - pos);
            if (dist < bestDist)
            {
                bestDist = dist;
                bestOnset = *it;
            }
            ++it;
        }

        if (bestDist <= windowSec)
            refined[i] = bestOnset;
    }

    return refined;
}
