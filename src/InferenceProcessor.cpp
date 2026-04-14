#if REABEAT_HAS_ONNX

#include "InferenceProcessor.h"
#include <algorithm>
#include <cstring>

InferenceProcessor::InferenceProcessor(Ort::Session& session)
    : session_(session) {}

// Zero-pad a spectrogram chunk
static std::vector<std::vector<float>> zeropad(
    const std::vector<std::vector<float>>& spect, int left, int right)
{
    if (left == 0 && right == 0)
        return spect;

    int cols = spect.empty() ? 0 : static_cast<int>(spect[0].size());
    int rows = static_cast<int>(spect.size()) + left + right;
    std::vector<std::vector<float>> padded(rows, std::vector<float>(cols, 0.0f));

    for (size_t i = 0; i < spect.size(); ++i)
        padded[i + left] = spect[i];

    return padded;
}

// FIX: Returns both chunks AND starts (original computed starts twice)
InferenceProcessor::SplitResult InferenceProcessor::splitPiece(
    const std::vector<std::vector<float>>& spect)
{
    SplitResult result;
    int lenSpect = static_cast<int>(spect.size());
    int step = kChunkSize - 2 * kBorderSize;

    // Generate start positions
    for (int start = -kBorderSize; start < lenSpect - kBorderSize; start += step)
        result.starts.push_back(start);

    // Adjust last start to avoid short end chunk
    if (lenSpect > step && !result.starts.empty())
        result.starts.back() = lenSpect - (kChunkSize - kBorderSize);

    // Extract chunks
    for (int start : result.starts)
    {
        int actualStart = std::max(0, start);
        int actualEnd = std::min(start + kChunkSize, lenSpect);

        std::vector<std::vector<float>> chunk;
        if (actualEnd > actualStart)
            chunk.assign(spect.begin() + actualStart, spect.begin() + actualEnd);

        int leftPad = std::max(0, -start);
        int rightPad = std::max(0, std::min(kBorderSize, start + kChunkSize - lenSpect));

        result.chunks.push_back(zeropad(chunk, leftPad, rightPad));
    }

    return result;
}

std::pair<std::vector<float>, std::vector<float>>
InferenceProcessor::runInference(const std::vector<std::vector<float>>& chunk)
{
    int numFrames = static_cast<int>(chunk.size());
    int numMels = chunk.empty() ? 128 : static_cast<int>(chunk[0].size());

    // Flatten spectrogram to 1D
    size_t tensorSize = numFrames * numMels;
    std::vector<float> inputData(tensorSize);
    for (int i = 0; i < numFrames; ++i)
        std::memcpy(inputData.data() + i * numMels, chunk[i].data(), numMels * sizeof(float));

    std::vector<int64_t> inputShape = {1, static_cast<int64_t>(numFrames), static_cast<int64_t>(numMels)};

    auto memInfo = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    auto inputTensor = Ort::Value::CreateTensor<float>(
        memInfo, inputData.data(), tensorSize, inputShape.data(), inputShape.size());

    const char* inputNames[] = {"input_spectrogram"};
    const char* outputNames[] = {"beat", "downbeat"};

    auto outputs = session_.Run(Ort::RunOptions{nullptr},
                                inputNames, &inputTensor, 1,
                                outputNames, 2);

    float* beatData = outputs[0].GetTensorMutableData<float>();
    float* downbeatData = outputs[1].GetTensorMutableData<float>();
    auto shape = outputs[0].GetTensorTypeAndShapeInfo().GetShape();
    size_t outputSize = shape[0] * shape[1];

    return {
        std::vector<float>(beatData, beatData + outputSize),
        std::vector<float>(downbeatData, downbeatData + outputSize)
    };
}

std::pair<std::vector<float>, std::vector<float>>
InferenceProcessor::aggregate(
    const std::vector<std::pair<std::vector<float>, std::vector<float>>>& predChunks,
    const std::vector<int>& starts,
    int fullSize)
{
    std::vector<float> beatResult(fullSize, -1000.0f);
    std::vector<float> downbeatResult(fullSize, -1000.0f);

    // Process in reverse (overlap_mode = "keep_first")
    for (int i = static_cast<int>(predChunks.size()) - 1; i >= 0; --i)
    {
        const auto& [beatChunk, downbeatChunk] = predChunks[i];
        int start = starts[i];

        int jStart = kBorderSize;
        int jEnd = static_cast<int>(beatChunk.size()) - kBorderSize;

        // Handle chunks smaller than 2*border
        if (static_cast<int>(beatChunk.size()) < 2 * kBorderSize)
        {
            jStart = 0;
            jEnd = static_cast<int>(beatChunk.size());
        }

        for (int j = jStart; j < jEnd; ++j)
        {
            int target = start + j;
            if (target >= 0 && target < fullSize)
            {
                beatResult[target] = beatChunk[j];
                downbeatResult[target] = downbeatChunk[j];
            }
        }
    }

    return {beatResult, downbeatResult};
}

std::pair<std::vector<float>, std::vector<float>>
InferenceProcessor::process(const std::vector<std::vector<float>>& spectrogram,
                            std::function<void(float)> progressCb)
{
    auto [chunks, starts] = splitPiece(spectrogram);

    std::vector<std::pair<std::vector<float>, std::vector<float>>> predChunks;
    predChunks.reserve(chunks.size());

    for (size_t i = 0; i < chunks.size(); ++i)
    {
        predChunks.push_back(runInference(chunks[i]));

        if (progressCb)
            progressCb(static_cast<float>(i + 1) / static_cast<float>(chunks.size()));
    }

    return aggregate(predChunks, starts, static_cast<int>(spectrogram.size()));
}

#endif // REABEAT_HAS_ONNX
