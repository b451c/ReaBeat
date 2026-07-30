#include "ModelManager.h"
#include <juce_core/juce_core.h>

#ifdef _WIN32
  #include <windows.h>
#else
  #include <dlfcn.h>
#endif

// Resolve the directory containing this plugin binary. Used to look for
// a portable model file next to the plugin (UserPlugins / portable REAPER).
static juce::File getPluginDirectory()
{
#ifdef _WIN32
    HMODULE hSelf = nullptr;
    GetModuleHandleExW(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        (LPCWSTR)&getPluginDirectory, &hSelf);
    if (!hSelf) return {};
    wchar_t buf[MAX_PATH] = {};
    if (GetModuleFileNameW(hSelf, buf, MAX_PATH) == 0) return {};
    return juce::File(juce::String(buf)).getParentDirectory();
#else
    Dl_info info{};
    if (dladdr((void*)&getPluginDirectory, &info) && info.dli_fname)
        return juce::File(juce::String::fromUTF8(info.dli_fname)).getParentDirectory();
    return {};
#endif
}

std::string ModelManager::getModelDir()
{
    auto home = juce::File::getSpecialLocation(juce::File::userHomeDirectory);
    return home.getChildFile(".reabeat").getChildFile("models").getFullPathName().toStdString();
}

// A candidate model file counts only when its size is plausible; a
// truncated portable file must not shadow a good downloaded one.
static bool isPlausibleModelFile(const juce::File& f)
{
    if (!f.existsAsFile())
        return false;
    auto size = f.getSize();
    return size >= static_cast<juce::int64>(ModelManager::kExpectedSizeMin)
        && size <= static_cast<juce::int64>(ModelManager::kExpectedSizeMax);
}

std::string ModelManager::getModelPath()
{
    // Search order:
    //   1. Plugin directory (portable: UserPlugins/beat_this_final0.onnx)
    //   2. Plugin directory / "ReaBeat" / "models" subfolder (organized portable)
    //   3. ~/.reabeat/models/ (auto-download default)
    auto pluginDir = getPluginDirectory();
    if (pluginDir != juce::File())
    {
        auto flat = pluginDir.getChildFile(kModelFilename);
        if (isPlausibleModelFile(flat))
            return flat.getFullPathName().toStdString();

        auto sub = pluginDir.getChildFile("ReaBeat").getChildFile("models").getChildFile(kModelFilename);
        if (isPlausibleModelFile(sub))
            return sub.getFullPathName().toStdString();
    }

    auto home = juce::File(getModelDir()).getChildFile(kModelFilename);
    if (isPlausibleModelFile(home))
        return home.getFullPathName().toStdString();

    return {};
}

bool ModelManager::isModelCached()
{
    // getModelPath only returns size-validated candidates
    return !getModelPath().empty();
}

bool ModelManager::downloadModel(std::function<void(float)> progressCb,
                                 std::function<bool()> shouldCancel)
{
    // Create directory
    auto dir = juce::File(getModelDir());
    if (!dir.exists())
        dir.createDirectory();

    auto destFile = dir.getChildFile(kModelFilename);

    // Download into a temp file and rename into place only after full
    // validation. Writing destFile directly is unsafe twice over:
    // juce::FileOutputStream APPENDS to an existing file, so a leftover
    // partial download (killed process) would grow into a size-plausible
    // corrupt model; and a concurrent isModelCached() could see a
    // half-written file as valid.
    auto tempFile = dir.getChildFile(juce::String(kModelFilename) + ".part");
    tempFile.deleteFile();

    // Download URL - hosted on GitHub Releases
    juce::URL url("https://github.com/b451c/ReaBeat/releases/download/v2.0.0-model/beat_this_final0.onnx");

    // Use JUCE URL download with progress
    auto inputStream = url.createInputStream(
        juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
            .withConnectionTimeoutMs(30000)
            .withStatusCode(nullptr));

    if (!inputStream)
        return false;

    // Get content length for progress + completeness validation
    auto contentLength = inputStream->getTotalLength();

    auto outputStream = tempFile.createOutputStream();
    if (!outputStream)
        return false;

    // Download in chunks
    constexpr int kBufferSize = 65536;
    juce::HeapBlock<char> buffer(kBufferSize);
    juce::int64 totalRead = 0;

    while (!inputStream->isExhausted())
    {
        if (shouldCancel && shouldCancel())
        {
            outputStream.reset();
            tempFile.deleteFile();
            return false;
        }

        auto bytesRead = inputStream->read(buffer.getData(), kBufferSize);
        if (bytesRead <= 0)
            break;

        if (!outputStream->write(buffer.getData(), static_cast<size_t>(bytesRead)))
        {
            // Disk full or I/O error - a silent break here would leave a
            // truncated file that can pass the size check below
            outputStream.reset();
            tempFile.deleteFile();
            return false;
        }
        totalRead += bytesRead;

        if (progressCb && contentLength > 0)
            progressCb(static_cast<float>(totalRead) / static_cast<float>(contentLength));
    }

    outputStream->flush();
    bool writeOk = outputStream->getStatus().wasOk();
    outputStream.reset();

    // Validate: write status, completeness vs Content-Length (a dropped
    // connection past 70 MB would otherwise pass the size range check),
    // and plausible size
    auto size = tempFile.getSize();
    bool sizeOk = size >= static_cast<juce::int64>(kExpectedSizeMin)
               && size <= static_cast<juce::int64>(kExpectedSizeMax);
    bool complete = contentLength <= 0 || totalRead == contentLength;

    if (!writeOk || !sizeOk || !complete)
    {
        tempFile.deleteFile();
        return false;
    }

    destFile.deleteFile();
    if (!tempFile.moveFileTo(destFile))
    {
        tempFile.deleteFile();
        return false;
    }

    return true;
}
