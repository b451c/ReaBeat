#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <string>
#include <functional>

// Download, cache, and manage the ONNX model file.
// Model is downloaded on first use to ~/.reabeat/models/

class ModelManager
{
public:
    // Get path to model file. Returns empty string if not available.
    static std::string getModelPath();

    // Check if model is already cached locally.
    static bool isModelCached();

    // Get the model directory path.
    static std::string getModelDir();

    // Download model from URL with progress callback.
    // progressCb: (fraction 0-1)
    // shouldCancel: polled per chunk; abort + clean up when it returns true.
    // Returns true on success.
    static bool downloadModel(std::function<void(float)> progressCb = nullptr,
                              std::function<bool()> shouldCancel = nullptr);

    // Model file identity check is by name + plausible size range only
    // (the real model is ~79 MB; anything outside this range is treated
    // as corrupt/truncated and ignored or deleted).
    static constexpr const char* kModelFilename = "beat_this_final0.onnx";
    static constexpr size_t kExpectedSizeMin = 70'000'000;   // ~70MB minimum
    static constexpr size_t kExpectedSizeMax = 100'000'000;  // ~100MB maximum
};
