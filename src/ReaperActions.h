#pragma once

#include "reaper_plugin.h"
#include "reaper_plugin_functions.h"
#include <vector>
#include <string>

// Direct REAPER API action calls for beat detection results.
// Port of reabeat_actions.lua - all actions wrapped in undo blocks.

class ReaperActions
{
public:
    // Insert stretch markers at beat positions.
    // Returns count of markers inserted, or 0 if cancelled.
    // quantizeMode: 1=off, 2=straight (mathematical grid), 3=bars (downbeat subdivision), 4=project grid (REAPER grid)
    static int insertStretchMarkers(
        MediaItem_Take* take,
        MediaItem* item,
        const std::vector<float>& beatTimes,
        int quantizeMode,
        int stretchFlag,       // 1=balanced, 2=tonal, 4=transient, 0=no override
        float detectedBpm = 0,
        const std::vector<float>& downbeats = {},
        int timeSigNum = 4,
        float strength = 1.0f); // 0.0=no change, 1.0=full quantize

    // Insert tempo map markers.
    // mode: "constant", "variable_bars", "variable_beats"
    // Returns count of markers inserted, or 0 if cancelled.
    static int insertTempoMap(
        MediaItem_Take* take,
        MediaItem* item,
        float tempo,
        const std::vector<float>& beatList,
        int beatsPerMarker,
        int timeSigNum,
        int timeSigDenom,
        const std::string& mode);

    // Match item playrate to target BPM (pitch preserved).
    // firstDownbeatSrc: source position of first downbeat for auto-align (-1 to skip).
    // Returns true on success.
    static bool matchTempo(
        MediaItem_Take* take,
        MediaItem* item,
        float detectedBpm,
        float targetBpm,
        float firstDownbeatSrc = -1.0f);

    // --- Single stretch marker operations (marker edit mode) ---

    // Add one stretch marker. Returns REAPER marker index, or -1 on failure.
    static int addOneStretchMarker(MediaItem_Take* take, MediaItem* item,
                                    double src, double dst);

    // Move one stretch marker by source position. Returns true on success.
    static bool moveOneStretchMarker(MediaItem_Take* take, MediaItem* item,
                                      double oldSrc, double newSrc);

    // Delete one stretch marker by source position. Returns true on success.
    static bool deleteOneStretchMarker(MediaItem_Take* take, MediaItem* item,
                                        double src);

    // Compute destination for a new marker by interpolating adjacent markers.
    static double interpolateDst(MediaItem_Take* take, double newSrc);

    // Get current project BPM.
    static float getProjectBpm();

    // Set project BPM (master tempo).
    static void setProjectBpm(float bpm);
};
