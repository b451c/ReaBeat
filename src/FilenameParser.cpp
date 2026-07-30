#include "FilenameParser.h"
#include <regex>
#include <algorithm>
#include <cctype>

FilenameHints FilenameParser::parse(const std::string& filename)
{
    FilenameHints hints;

    // Extract just the filename (no path, no extension)
    auto lastSlash = filename.find_last_of("/\\");
    std::string name = (lastSlash != std::string::npos) ? filename.substr(lastSlash + 1) : filename;
    auto lastDot = name.find_last_of('.');
    if (lastDot != std::string::npos)
        name = name.substr(0, lastDot);

    // Case-insensitive matching: convert to lowercase
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(),
        [](unsigned char c) { return std::tolower(c); });

    // BPM patterns: "120bpm", "120_bpm", "120 bpm", "bpm120", "bpm_120", "bpm 120".
    // Digit boundaries stop trailing digits of longer numbers from matching
    // ("loop_44100_bpm" must not read 100 BPM out of a sample rate), and
    // iterating ALL matches lets a later valid one win when the first is
    // out of range ("1984_bpm_120" -> 120, not nothing).
    std::regex bpmAfter(R"((?:^|[^0-9])(\d{2,3})\s*[_\-]?\s*bpm)");
    std::regex bpmBefore(R"(bpm\s*[_\-]?\s*(\d{2,3})(?![0-9]))");

    auto firstValidBpm = [&lower](const std::regex& re) -> float
    {
        for (auto it = std::sregex_iterator(lower.begin(), lower.end(), re);
             it != std::sregex_iterator(); ++it)
        {
            int bpm = std::stoi((*it)[1].str());
            if (bpm >= 40 && bpm <= 300)
                return static_cast<float>(bpm);
        }
        return 0.0f;
    };

    hints.bpm = firstValidBpm(bpmAfter);
    if (hints.bpm <= 0)
        hints.bpm = firstValidBpm(bpmBefore);

    // Time signature patterns: "4-4", "3/4", "4_4", "6-8", "7/8"
    // Numerator 2-7, denominator 4 or 8. Digit boundaries keep dates and
    // longer numbers from matching ("session_2024-8" is not 4/8).
    std::regex timeSig(R"((?:^|[^0-9])(\d)[/\-_](\d)(?![0-9]))");
    for (auto it = std::sregex_iterator(lower.begin(), lower.end(), timeSig);
         it != std::sregex_iterator(); ++it)
    {
        int num = std::stoi((*it)[1].str());
        int den = std::stoi((*it)[2].str());
        if (num >= 2 && num <= 7 && (den == 4 || den == 8))
        {
            hints.timeSigNum = num;
            hints.timeSigDenom = den;
            break;
        }
    }

    return hints;
}
