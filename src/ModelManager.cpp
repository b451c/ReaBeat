#include "ModelManager.h"
#include <juce_core/juce_core.h>

std::string ModelManager::getModelDir()
{
#if defined(__APPLE__)
    auto home = juce::File::getSpecialLocation(juce::File::userHomeDirectory);
#elif defined(_WIN32)
    auto home = juce::File::getSpecialLocation(juce::File::userHomeDirectory);
#else
    auto home = juce::File::getSpecialLocation(juce::File::userHomeDirectory);
#endif

    return home.getChildFile(".reabeat").getChildFile("models").getFullPathName().toStdString();
}

std::string ModelManager::getModelPath()
{
    auto dir = getModelDir();
    auto path = juce::File(dir).getChildFile(kModelFilename).getFullPathName().toStdString();

    if (juce::File(path).existsAsFile())
        return path;

    return {};
}

bool ModelManager::isModelCached()
{
    auto path = getModelPath();
    if (path.empty()) return false;

    // Basic size validation
    auto size = juce::File(path).getSize();
    return size >= kExpectedSizeMin && size <= kExpectedSizeMax;
}

bool ModelManager::downloadModel(std::function<void(float)> progressCb)
{
    // Create directory
    auto dir = juce::File(getModelDir());
    if (!dir.exists())
        dir.createDirectory();

    auto destFile = dir.getChildFile(kModelFilename);

    // Download URL - hosted on GitHub Releases
    juce::URL url("https://github.com/b451c/ReaBeat/releases/download/v2.0.0-model/beat_this_final0.onnx");

    // Use JUCE URL download with progress
    auto inputStream = url.createInputStream(
        juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
            .withConnectionTimeoutMs(30000)
            .withStatusCode(nullptr));

    if (!inputStream)
        return false;

    // Get content length for progress
    auto contentLength = inputStream->getTotalLength();

    auto outputStream = destFile.createOutputStream();
    if (!outputStream)
        return false;

    // Download in chunks
    constexpr int kBufferSize = 65536;
    juce::HeapBlock<char> buffer(kBufferSize);
    juce::int64 totalRead = 0;

    while (!inputStream->isExhausted())
    {
        auto bytesRead = inputStream->read(buffer.getData(), kBufferSize);
        if (bytesRead <= 0)
            break;

        outputStream->write(buffer.getData(), static_cast<size_t>(bytesRead));
        totalRead += bytesRead;

        if (progressCb && contentLength > 0)
            progressCb(static_cast<float>(totalRead) / static_cast<float>(contentLength));
    }

    outputStream->flush();
    outputStream.reset();

    // Validate downloaded file size
    auto size = destFile.getSize();
    if (size < kExpectedSizeMin || size > kExpectedSizeMax)
    {
        destFile.deleteFile();
        return false;
    }

    return true;
}
