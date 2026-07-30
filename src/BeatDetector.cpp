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
        // ONNX Runtime on Windows requires wide string path.
        // modelPath is UTF-8 (JUCE convention) - a byte-for-byte copy into
        // wchar_t corrupts any non-ASCII path (e.g. Cyrillic/CJK/Polish
        // Windows usernames), so convert properly.
        std::wstring widePath;
        int wlen = MultiByteToWideChar(CP_UTF8, 0, modelPath.c_str(), -1, nullptr, 0);
        if (wlen > 1)
        {
            widePath.resize(static_cast<size_t>(wlen - 1));
            MultiByteToWideChar(CP_UTF8, 0, modelPath.c_str(), -1, widePath.data(), wlen);
        }
        session_ = std::make_unique<Ort::Session>(*env_, widePath.c_str(), opts);
#else
        session_ = std::make_unique<Ort::Session>(*env_, modelPath.c_str(), opts);
#endif
        modelLoaded_ = true;
        return true;
    }
    catch (const std::exception&)
    {
        // Ort::Exception derives from std::exception; also covers bad_alloc
        // while mapping the 79 MB model on a memory-starved system.
        modelLoaded_ = false;
        return false;
    }
    catch (...)
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

float BeatDetector::computeConfidence(const std::vector<float>& beats)
{
    if (beats.size() < 4)
        return 0.5f;

    // Measure interval consistency against the median inter-beat interval,
    // NOT against 60/tempo: the reported tempo is octave-corrected into
    // 78-185 BPM, so for e.g. a steady 70 BPM track it is 140 and every
    // interval would "deviate" 100%, reporting 0% for a perfect detection.
    std::vector<float> intervals;
    intervals.reserve(beats.size() - 1);
    for (size_t i = 1; i < beats.size(); ++i)
        intervals.push_back(beats[i] - beats[i - 1]);

    auto sorted = intervals;
    std::sort(sorted.begin(), sorted.end());
    float medianIbi = sorted[sorted.size() / 2];
    if (medianIbi <= 0.0f)
        return 0.0f;

    int consistent = 0;
    for (float ibi : intervals)
    {
        float deviation = std::abs(ibi - medianIbi) / medianIbi;
        if (deviation < 0.10f)
            ++consistent;
    }

    return std::min(1.0f, static_cast<float>(consistent) / static_cast<float>(intervals.size()));
}


DetectionResult BeatDetector::detect(const std::vector<float>& audioMono,
                                      int sampleRate,
                                      std::function<void(const std::string&, float)> progressCb,
                                      std::function<bool()> shouldCancel)
{
    DetectionResult result;
    auto t0 = std::chrono::steady_clock::now();

    auto cancelled = [&]() { return shouldCancel && shouldCancel(); };

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

    if (cancelled())
    {
        result.error = kCancelledError;
        return result;
    }

    // Resample + mel spectrogram allocate hundreds of MB for very long
    // files (90 min: ~476 MB resample output while the mono buffer is
    // still alive, ~140 MB+ mel). An uncaught bad_alloc here would escape
    // to the juce::Thread and std::terminate the whole REAPER process.
    std::vector<float> audio22k;
    std::vector<std::vector<float>> spectrogram;
    try
    {
        audio22k = resampleTo22050(audioMono, sampleRate);

        if (progressCb) progressCb("Computing spectrogram...", 0.1f);

        if (cancelled())
        {
            result.error = kCancelledError;
            return result;
        }

        MelSpectrogram mel;
        spectrogram = mel.compute(audio22k);
    }
    catch (const std::bad_alloc&)
    {
        result.error = "Out of memory preparing audio. The item may be too long for available RAM - try splitting it.";
        return result;
    }
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
    struct DetectionCancelled {};
    InferenceProcessor inference(*session_);
    try {
    auto [beatLogits, downbeatLogits] = inference.process(spectrogram,
        [&](float frac) {
            // Thrown between chunks (not inside Ort::Run), caught below -
            // gives per-chunk cancellation granularity (~30 s of audio).
            if (cancelled())
                throw DetectionCancelled{};
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

    // Onset refinement: snap beats/downbeats to nearest audio transient (+/-30ms).
    // Uses spectral flux STFT with hop=64 samples (~344 fps at 22050 Hz). For very
    // long audio this builds a frames x 513-bin magnitude matrix that runs to
    // multiple GB (90 min would need ~3.8 GB just for that matrix and OOMs).
    // The neural model already gives ~20 ms precision which is fine for most
    // workflows, so skip refinement past kRefinementMaxSec.
    constexpr float kRefinementMaxSec = 600.0f;  // 10 min
    bool refine = (audio22k.size() / 22050.0) <= kRefinementMaxSec;

    if (cancelled())
    {
        result.error = kCancelledError;
        return result;
    }

    std::vector<float> refinedBeats;
    std::vector<float> rawDownbeats;
    if (refine)
    {
        if (progressCb) progressCb("Refining to transients...", 0.80f);
        refinedBeats = OnsetRefinement::refine(audio22k, 22050, interpolatedBeats);
        rawDownbeats = OnsetRefinement::refine(audio22k, 22050, ppResult.downbeatTimes);
    }
    else
    {
        if (progressCb) progressCb("Skipping refinement (long audio)...", 0.80f);
        refinedBeats = interpolatedBeats;
        rawDownbeats = ppResult.downbeatTimes;
    }

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
    result.confidence = computeConfidence(result.beats);

    auto t1 = std::chrono::steady_clock::now();
    result.detectionTime = std::chrono::duration<float>(t1 - t0).count();

    if (progressCb) progressCb("Done", 1.0f);
    } catch (const DetectionCancelled&) {
        result.error = kCancelledError;
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
                                          std::function<void(const std::string&, float)> progressCb,
                                          std::function<bool()> shouldCancel)
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
    // int64: an int would overflow past ~3.1 h at 192 kHz and report
    // "Invalid audio source" for a perfectly valid file
    auto numSamples = static_cast<int64_t>(lengthSec * sampleRate);

    if (sampleRate < 1 || numSamples < 1)
    {
        delete source;
        DetectionResult result;
        result.error = "Invalid audio source (MIDI or empty)";
        return result;
    }

    // Read audio in 60-second chunks and downmix to mono inline.
    // A monolithic GetSamples() call would need ~7.6 GB for a 90-minute
    // stereo file (ReaSample is double = 8 bytes per sample, per channel)
    // which OOMs on most systems. Chunked reading keeps peak interleaved
    // buffer to a fixed ~42 MB regardless of file length.
    std::vector<float> mono;
    try
    {
        mono.reserve(static_cast<size_t>(numSamples));

        constexpr int kChunkSec = 60;
        int chunkSamples = sampleRate * kChunkSec;
        std::vector<ReaSample> chunkBuf(static_cast<size_t>(chunkSamples) * numChannels);

        PCM_source_transfer_t transfer = {};
        transfer.samplerate = sampleRate;
        transfer.nch = numChannels;
        transfer.samples = chunkBuf.data();

        int64_t offsetSamples = 0;
        while (offsetSamples < numSamples)
        {
            if (shouldCancel && shouldCancel())
            {
                delete source;
                DetectionResult result;
                result.error = kCancelledError;
                return result;
            }

            int thisChunk = static_cast<int>(
                std::min<int64_t>(chunkSamples, numSamples - offsetSamples));
            transfer.time_s = static_cast<double>(offsetSamples) / sampleRate;
            transfer.length = thisChunk;
            transfer.samples_out = 0;

            source->GetSamples(&transfer);
            int read = transfer.samples_out;
            if (read < 1)
                break;

            if (numChannels == 1)
            {
                for (int i = 0; i < read; ++i)
                    mono.push_back(static_cast<float>(chunkBuf[i]));
            }
            else
            {
                for (int i = 0; i < read; ++i)
                {
                    double sum = 0;
                    for (int ch = 0; ch < numChannels; ++ch)
                        sum += chunkBuf[static_cast<size_t>(i) * numChannels + ch];
                    mono.push_back(static_cast<float>(sum / numChannels));
                }
            }

            offsetSamples += read;
            if (progressCb && numSamples > 0)
                progressCb("Reading audio...",
                           0.05f * static_cast<float>(offsetSamples)
                                 / static_cast<float>(numSamples));
        }
    }
    catch (const std::bad_alloc&)
    {
        delete source;
        DetectionResult result;
        result.error = "Out of memory reading audio. The item may be too long for available RAM - try splitting it.";
        return result;
    }

    delete source;

    if (mono.empty())
    {
        DetectionResult result;
        result.error = "Failed to read audio samples";
        return result;
    }

    return detect(mono, sampleRate, progressCb, shouldCancel);
}
