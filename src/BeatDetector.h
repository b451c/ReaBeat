#pragma once

#include <vector>
#include <string>
#include <functional>
#include <memory>

#if REABEAT_HAS_ONNX
#include <onnxruntime_cxx_api.h>
#endif

// High-level beat detection API.
// Wraps: MelSpectrogram -> InferenceProcessor -> Postprocessor -> algorithms

struct DetectionResult
{
    std::vector<float> beats;
    std::vector<float> downbeats;
    float tempo = 0.0f;
    int timeSigNum = 4;
    int timeSigDenom = 4;
    float confidence = 0.0f;
    float duration = 0.0f;
    float detectionTime = 0.0f;
    std::vector<float> peaks;  // RMS waveform envelope (100 values/sec, 0-1 range)
    std::string error;  // non-empty on failure
};

class BeatDetector
{
public:
    BeatDetector();
    ~BeatDetector();

    // Load ONNX model. Returns false on failure.
    bool loadModel(const std::string& modelPath);

    // Is model loaded and ready?
    bool isReady() const;

    // Run detection on mono audio at given sample rate.
    // progressCb: (message, fraction 0-1)
    // shouldCancel: polled between stages; when it returns true, detection
    // stops early and the result carries error == kCancelledError.
    DetectionResult detect(const std::vector<float>& audioMono,
                           int sampleRate,
                           std::function<void(const std::string&, float)> progressCb = nullptr,
                           std::function<bool()> shouldCancel = nullptr);

    // Run detection on audio file path.
    DetectionResult detectFile(const std::string& filePath,
                               std::function<void(const std::string&, float)> progressCb = nullptr,
                               std::function<bool()> shouldCancel = nullptr);

    // error string used when detection was cancelled via shouldCancel
    static constexpr const char* kCancelledError = "Cancelled";

private:
    // Resample audio to 22050 Hz mono
    std::vector<float> resampleTo22050(const std::vector<float>& audio, int srcRate);

    // Confidence = fraction of inter-beat intervals within 10% of the median
    float computeConfidence(const std::vector<float>& beats);

#if REABEAT_HAS_ONNX
    std::unique_ptr<Ort::Env> env_;
    std::unique_ptr<Ort::Session> session_;
#endif
    bool modelLoaded_ = false;
};
