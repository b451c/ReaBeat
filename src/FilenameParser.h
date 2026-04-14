#pragma once
#include <string>

// Parse BPM and meter hints from audio filename.
// Hipox's feature request - many files encode tempo in their name.

struct FilenameHints
{
    float bpm = 0;          // 0 = not found
    int timeSigNum = 0;     // 0 = not found
    int timeSigDenom = 0;   // 0 = not found
};

class FilenameParser
{
public:
    // Parse BPM and meter from filename (not full path).
    // Matches: "120bpm", "120_bpm", "BPM120", "4-4", "3/4", etc.
    static FilenameHints parse(const std::string& filename);
};
