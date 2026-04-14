#pragma once

#if REABEAT_HAS_ONNX

#include <vector>
#include <functional>
#include <onnxruntime_cxx_api.h>

// ONNX model inference with chunking for beat detection.
// Port of beat_this_cpp InferenceProcessor with fixes:
// - split_piece returns starts (no double computation)
// - Progress callback support

class InferenceProcessor
{
public:
    explicit InferenceProcessor(Ort::Session& session);

    // Process full spectrogram, return {beat_logits, downbeat_logits}
    std::pair<std::vector<float>, std::vector<float>>
    process(const std::vector<std::vector<float>>& spectrogram,
            std::function<void(float)> progressCb = nullptr);

private:
    Ort::Session& session_;

    static constexpr int kChunkSize = 1500;   // 30 seconds at 50fps
    static constexpr int kBorderSize = 6;     // 120ms overlap

    struct SplitResult
    {
        std::vector<std::vector<std::vector<float>>> chunks;
        std::vector<int> starts;
    };

    SplitResult splitPiece(const std::vector<std::vector<float>>& spect);

    std::pair<std::vector<float>, std::vector<float>>
    runInference(const std::vector<std::vector<float>>& chunk);

    std::pair<std::vector<float>, std::vector<float>>
    aggregate(const std::vector<std::pair<std::vector<float>, std::vector<float>>>& predChunks,
              const std::vector<int>& starts,
              int fullSize);
};

#endif // REABEAT_HAS_ONNX
