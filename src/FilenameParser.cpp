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

    // BPM patterns: "120bpm", "120_bpm", "120 bpm", "bpm120", "bpm_120", "bpm 120"
    std::regex bpmAfter(R"((\d{2,3})\s*[_\-]?\s*bpm)");
    std::regex bpmBefore(R"(bpm\s*[_\-]?\s*(\d{2,3}))");

    std::smatch match;
    if (std::regex_search(lower, match, bpmAfter))
    {
        int bpm = std::stoi(match[1].str());
        if (bpm >= 40 && bpm <= 300)
            hints.bpm = static_cast<float>(bpm);
    }
    else if (std::regex_search(lower, match, bpmBefore))
    {
        int bpm = std::stoi(match[1].str());
        if (bpm >= 40 && bpm <= 300)
            hints.bpm = static_cast<float>(bpm);
    }

    // Time signature patterns: "4-4", "3/4", "4_4", "6-8", "7/8"
    // Numerator 2-7, denominator 4 or 8
    std::regex timeSig(R"((\d)[/\-_](\d))");
    if (std::regex_search(lower, match, timeSig))
    {
        int num = std::stoi(match[1].str());
        int den = std::stoi(match[2].str());
        if (num >= 2 && num <= 7 && (den == 4 || den == 8))
        {
            hints.timeSigNum = num;
            hints.timeSigDenom = den;
        }
    }

    return hints;
}
