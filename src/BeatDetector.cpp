#include "BeatDetector.h"
#include "MelSpectrogram.h"
#include "Postprocessor.h"
#include "TempoEstimator.h"
#include "DownbeatCleaner.h"
#include "TimeSigDetector.h"
#include "BeatInterpolator.h"
#include "OnsetRefinement.h"

#if REABEAT_HAS_ONNX
#include "InferenceProcessor.h"
#endif

#include "reaper_plugin.h"
#include "reaper_plugin_functions.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <chrono>
#include <cmath>
#include <numeric>

BeatDetector::BeatDetector() = default;
BeatDetector::~BeatDetector() = default;

bool BeatDetector::loadModel(const std::string& modelPath)
{
#if REABEAT_HAS_ONNX
    try
    {
        env_ = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "ReaBeat");

        Ort::SessionOptions opts;
        opts.SetIntraOpNumThreads(4);
        opts.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

#ifdef _WIN32
        // ONNX Runtime on Windows requires wide string path
        std::wstring widePath(modelPath.begin(), modelPath.end());
        session_ = std::make_unique<Ort::Session>(*env_, widePath.c_str(), opts);
#else
        session_ = std::make_unique<Ort::Session>(*env_, modelPath.c_str(), opts);
#endif
        modelLoaded_ = true;
        return true;
    }
    catch (const Ort::Exception& e)
    {
        modelLoaded_ = false;
        return false;
    }
#else
    (void)modelPath;
    return false;
#endif
}

bool BeatDetector::isReady() const
{
    return modelLoaded_;
}

std::vector<float> BeatDetector::resampleTo22050(const std::vector<float>& audio, int srcRate)
{
    if (srcRate == 22050)
        return audio;

    double ratio = 22050.0 / srcRate;
    auto outSize = static_cast<size_t>(std::ceil(audio.size() * ratio));
    std::vector<float> output(outSize);

    // JUCE LagrangeInterpolator for high-quality resampling
    juce::LagrangeInterpolator interpolator;
    interpolator.reset();

    int numUsed = 0;
    interpolator.process(1.0 / ratio, audio.data(), output.data(),
                         static_cast<int>(outSize), static_cast<int>(audio.size()),
                         numUsed);

    return output;
}

float BeatDetector::computeConfidence(const std::vector<float>& beats, float tempo)
{
    if (beats.size() < 4)
        return 0.5f;

    float expectedIbi = 60.0f / tempo;
    int consistent = 0;

    for (size_t i = 1; i < beats.size(); ++i)
    {
        float ibi = beats[i] - beats[i - 1];
        float deviation = std::abs(ibi - expectedIbi) / expectedIbi;
        if (deviation < 0.10f)
            ++consistent;
    }

    return std::min(1.0f, static_cast<float>(consistent) / static_cast<float>(beats.size() - 1));
}


DetectionResult BeatDetector::detect(const std::vector<float>& audioMono,
                                      int sampleRate,
                                      std::function<void(const std::string&, float)> progressCb)
{
    DetectionResult result;
    auto t0 = std::chrono::steady_clock::now();

    if (!modelLoaded_)
    {
        result.error = "Model not loaded";
        return result;
    }

    if (progressCb) progressCb("Preparing audio...", 0.0f);

    // Validate audio
    result.duration = static_cast<float>(audioMono.size()) / sampleRate;
    if (result.duration < 2.0f)
    {
        result.error = "Audio too short (minimum 2 seconds)";
        return result;
    }

    // Check silence
    double rmsTotal = 0;
    for (float s : audioMono) rmsTotal += s * s;
    rmsTotal = std::sqrt(rmsTotal / audioMono.size());
    if (rmsTotal < 0.001)
    {
        result.error = "Audio is silent";
        return result;
    }

    // Compute waveform peaks (100 values/sec for UI display)
    {
        int peaksPerSec = 100;
        int hop = std::max(1, sampleRate / peaksPerSec);
        int nFrames = static_cast<int>(audioMono.size()) / hop;
        result.peaks.resize(nFrames);

        for (int i = 0; i < nFrames; ++i)
        {
            float sum = 0;
            int base = i * hop;
            for (int j = 0; j < hop && base + j < static_cast<int>(audioMono.size()); ++j)
            {
                float s = audioMono[base + j];
                sum += s * s;
            }
            result.peaks[i] = std::sqrt(sum / hop);
        }

        // Normalize by 98th percentile
        if (!result.peaks.empty())
        {
            auto sorted = result.peaks;
            std::sort(sorted.begin(), sorted.end());
            float p98 = sorted[static_cast<size_t>(sorted.size() * 0.98)];
            if (p98 > 0)
                for (auto& p : result.peaks)
                    p = std::min(1.0f, p / p98);
        }
    }

    // Resample to 22050 Hz
    auto audio22k = resampleTo22050(audioMono, sampleRate);

    if (progressCb) progressCb("Computing spectrogram...", 0.1f);

    // Mel spectrogram
    MelSpectrogram mel;
    auto spectrogram = mel.compute(audio22k);
    if (spectrogram.empty())
    {
        result.error = "Failed to compute spectrogram";
        return result;
    }
    // Sanity check: detect NaN/Inf in spectrogram (corrupted audio or FFT bug)
    if (!spectrogram.empty() && !spectrogram[0].empty())
    {
        float v = spectrogram[0][0];
        if (std::isnan(v) || std::isinf(v))
        {
            result.error = "Spectrogram contains NaN/Inf values";
            return result;
        }
    }

#if REABEAT_HAS_ONNX
    if (progressCb) progressCb("Running neural network...", 0.2f);

    // ONNX inference
    InferenceProcessor inference(*session_);
    try {
    auto [beatLogits, downbeatLogits] = inference.process(spectrogram,
        [&](float frac) {
            if (progressCb)
                progressCb("Running neural network...", 0.2f + frac * 0.5f);
        });
    // Sanity check: detect NaN/Inf in inference output
    if (!beatLogits.empty() && (std::isnan(beatLogits[0]) || std::isinf(beatLogits[0])))
    {
        result.error = "Neural network output contains NaN/Inf values";
        return result;
    }

    if (progressCb) progressCb("Postprocessing...", 0.75f);

    // Postprocess: peak detection
    Postprocessor postproc(50.0f);
    auto ppResult = postproc.process(beatLogits, downbeatLogits);

    if (ppResult.beatTimes.size() < 2)
    {
        result.error = "Not enough beats detected";
        return result;
    }

    // Beat interpolation (fill gaps in quiet sections - squibs' 0.51x fix)
    auto interpolatedBeats = BeatInterpolator::interpolate(
        ppResult.beatTimes, ppResult.beatLogits, 50.0f);

    // Beat consistency pass: remove isolated false-positive beats
    // where BOTH neighboring intervals deviate > 25% from median.
    // Validated: +0.5% mean green, +2 tracks >= 95% on 53-track test.
    if (interpolatedBeats.size() >= 5)
    {
        std::vector<float> ivals;
        ivals.reserve(interpolatedBeats.size() - 1);
        for (size_t k = 1; k < interpolatedBeats.size(); ++k)
            ivals.push_back(interpolatedBeats[k] - interpolatedBeats[k - 1]);
        auto sortedIvals = ivals;
        std::sort(sortedIvals.begin(), sortedIvals.end());
        float medianIval = sortedIvals[sortedIvals.size() / 2];

        std::vector<float> consistent;
        consistent.push_back(interpolatedBeats[0]);
        for (size_t k = 1; k + 1 < interpolatedBeats.size(); ++k)
        {
            float prevGap = interpolatedBeats[k] - interpolatedBeats[k - 1];
            float nextGap = interpolatedBeats[k + 1] - interpolatedBeats[k];
            float prevDev = std::abs(prevGap - medianIval) / medianIval;
            float nextDev = std::abs(nextGap - medianIval) / medianIval;
            if (prevDev > 0.25f && nextDev > 0.25f)
                continue;  // isolated false positive - skip
            consistent.push_back(interpolatedBeats[k]);
        }
        consistent.push_back(interpolatedBeats.back());
        interpolatedBeats = std::move(consistent);
    }

    if (progressCb) progressCb("Refining to transients...", 0.80f);

    // Onset refinement: snap beats/downbeats to nearest audio transient (+/-30ms)
    // Uses spectral flux onset detection (replaces Python librosa)
    auto refinedBeats = OnsetRefinement::refine(audio22k, 22050, interpolatedBeats);
    auto rawDownbeats = OnsetRefinement::refine(audio22k, 22050, ppResult.downbeatTimes);

    result.beats = refinedBeats;

    if (progressCb) progressCb("Computing tempo...", 0.85f);

    // Tempo
    result.tempo = TempoEstimator::compute(result.beats);

    // Time signature and downbeats
    if (rawDownbeats.size() >= 2)
    {
        result.timeSigNum = TimeSigDetector::detect(result.beats, rawDownbeats);
        result.timeSigDenom = 4;
        result.downbeats = DownbeatCleaner::clean(rawDownbeats, result.tempo, result.timeSigNum);
    }
    else
    {
        result.timeSigNum = 4;
        result.timeSigDenom = 4;
        // Fallback: every Nth beat
        for (size_t i = 0; i < result.beats.size(); i += result.timeSigNum)
            result.downbeats.push_back(result.beats[i]);
    }

    // Confidence
    result.confidence = computeConfidence(result.beats, result.tempo);

    auto t1 = std::chrono::steady_clock::now();
    result.detectionTime = std::chrono::duration<float>(t1 - t0).count();

    if (progressCb) progressCb("Done", 1.0f);
    } catch (const std::exception& e) {
        result.error = std::string("Detection failed: ") + e.what();
    } catch (...) {
        result.error = "Detection failed: unknown error";
    }
#else
    result.error = "ONNX Runtime not available";
#endif

    return result;
}

DetectionResult BeatDetector::detectFile(const std::string& filePath,
                                          std::function<void(const std::string&, float)> progressCb)
{
    // Use REAPER API to read audio — supports ALL formats REAPER can open
    // (mp3, flac, ogg, opus, aac, wav, aiff, wma, etc.)
    if (!PCM_Source_CreateFromFile)
    {
        DetectionResult result;
        result.error = "REAPER API not available";
        return result;
    }

    PCM_source* source = PCM_Source_CreateFromFile(filePath.c_str());
    if (!source)
    {
        DetectionResult result;
        result.error = "Cannot read audio file: " + filePath;
        return result;
    }

    auto sampleRate = static_cast<int>(source->GetSampleRate());
    auto numChannels = source->GetNumChannels();
    auto lengthSec = source->GetLength();
    auto numSamples = static_cast<int>(lengthSec * sampleRate);

    if (sampleRate < 1 || numSamples < 1)
    {
        delete source;
        DetectionResult result;
        result.error = "Invalid audio source (MIDI or empty)";
        return result;
    }

    // Read all samples via REAPER's decoders
    std::vector<ReaSample> buffer(static_cast<size_t>(numSamples) * numChannels);

    PCM_source_transfer_t transfer = {};
    transfer.time_s = 0.0;
    transfer.samplerate = sampleRate;
    transfer.nch = numChannels;
    transfer.length = numSamples;
    transfer.samples = buffer.data();

    source->GetSamples(&transfer);
    delete source;

    int samplesRead = transfer.samples_out;
    if (samplesRead < 1)
    {
        DetectionResult result;
        result.error = "Failed to read audio samples";
        return result;
    }

    // Convert to mono
    std::vector<float> mono(samplesRead);
    if (numChannels == 1)
    {
        for (int i = 0; i < samplesRead; ++i)
            mono[i] = static_cast<float>(buffer[i]);
    }
    else
    {
        for (int i = 0; i < samplesRead; ++i)
        {
            double sum = 0;
            for (int ch = 0; ch < numChannels; ++ch)
                sum += buffer[static_cast<size_t>(i) * numChannels + ch];
            mono[i] = static_cast<float>(sum / numChannels);
        }
    }

    return detect(mono, sampleRate, progressCb);
}
