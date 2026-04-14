#include "ReaperActions.h"
#include <cmath>
#include <algorithm>
#include <cstdio>

// RAII guard: ensures PreventUIRefresh(-1) is always called, even on exception.
struct RefreshGuard {
    RefreshGuard()  { PreventUIRefresh(1); }
    ~RefreshGuard() { PreventUIRefresh(-1); }
};

// --- Helper: snap timeline position to nearest grid beat ---

static double snapToGrid(double timeline)
{
    int measures = 0;
    TimeMap2_timeToBeats(nullptr, timeline, &measures, nullptr, nullptr, nullptr);

    double barStart = TimeMap2_beatsToTime(nullptr, 0, &measures);
    int nextMeas = measures + 1;
    double barEnd = TimeMap2_beatsToTime(nullptr, 0, &nextMeas);
    double barDur = barEnd - barStart;

    int gridTsNum = 0;
    TimeMap_GetTimeSigAtTime(nullptr, timeline, &gridTsNum, nullptr, nullptr);
    if (gridTsNum == 0) gridTsNum = 4;

    double posInBar = timeline - barStart;
    double beatInBar = posInBar / barDur * gridTsNum;
    int nearest = static_cast<int>(std::max(0.0, std::min(static_cast<double>(gridTsNum),
        std::floor(beatInBar + 0.5))));
    double gridTime = barStart + (static_cast<double>(nearest) / gridTsNum) * barDur;

    // Reject if snap distance exceeds half-beat interval
    double halfBeat = barDur / gridTsNum / 2.0;
    if (std::abs(gridTime - timeline) >= halfBeat)
        gridTime = timeline;

    return gridTime;
}

// --- Helper: snap to nearest bar boundary ---

static double snapToNearestBar(double pos)
{
    int measures = 0;
    TimeMap2_timeToBeats(nullptr, pos, &measures, nullptr, nullptr, nullptr);

    double barStart = TimeMap2_beatsToTime(nullptr, 0, &measures);
    int nextMeas = measures + 1;
    double nextBar = TimeMap2_beatsToTime(nullptr, 0, &nextMeas);

    if (std::abs(pos - barStart) <= std::abs(pos - nextBar))
        return barStart;
    return nextBar;
}

// --- Insert Stretch Markers ---

int ReaperActions::insertStretchMarkers(
    MediaItem_Take* take,
    MediaItem* item,
    const std::vector<float>& beatTimes,
    int quantizeMode,
    int stretchFlag,
    float detectedBpm,
    const std::vector<float>& downbeats,
    int timeSigNum,
    float strength)
{
    if (!take || !item || beatTimes.empty())
        return 0;

    Undo_BeginBlock2(nullptr);
    RefreshGuard refreshGuard;

    // Clear existing stretch markers (always replace - Ctrl+Z to undo)
    int existing = GetTakeNumStretchMarkers(take);
    if (existing > 0)
    {
        int count = existing;
        DeleteTakeStretchMarkers(take, 0, &count);
    }

    double takeOffset = GetMediaItemTakeInfo_Value(take, "D_STARTOFFS");
    double playrate = GetMediaItemTakeInfo_Value(take, "D_PLAYRATE");
    double itemPos = GetMediaItemInfo_Value(item, "D_POSITION");
    double halfBeat = (detectedBpm > 0) ? 60.0 / detectedBpm / 2.0 : 0.25;

    struct Marker { double src; double dst; };
    std::vector<Marker> markers;

    // Helper: find nearest detected beat to target position (within halfBeat)
    auto findNearestBeat = [&](double target) -> double
    {
        auto it = std::lower_bound(beatTimes.begin(), beatTimes.end(),
                                   static_cast<float>(target));
        double nearest = target;
        double bestDist = halfBeat;

        if (it != beatTimes.end())
        {
            double d = std::abs(static_cast<double>(*it) - target);
            if (d < bestDist) { nearest = *it; bestDist = d; }
        }
        if (it != beatTimes.begin())
        {
            --it;
            double d = std::abs(static_cast<double>(*it) - target);
            if (d < bestDist) { nearest = *it; bestDist = d; }
        }
        return nearest;
    };

    // --- Helper: sequential gap rounding for mathematical grid ---
    // Filters false-positive beats, assigns each to a grid index via local
    // gap rounding (no drift accumulation). Returns filtered beats + indices.
    auto buildGridIndices = [&](double beatInterval)
        -> std::pair<std::vector<float>, std::vector<int>>
    {
        double minGap = beatInterval * 0.5;
        std::vector<float> filtered;
        filtered.reserve(beatTimes.size());
        for (float bt : beatTimes)
        {
            if (filtered.empty() || (bt - filtered.back()) >= minGap)
                filtered.push_back(bt);
        }

        std::vector<int> indices(filtered.size());
        indices[0] = 0;
        for (size_t i = 1; i < filtered.size(); ++i)
        {
            double gap = static_cast<double>(filtered[i] - filtered[i - 1]);
            int steps = std::max(1, static_cast<int>(std::round(gap / beatInterval)));
            indices[i] = indices[i - 1] + steps;
        }
        return {std::move(filtered), std::move(indices)};
    };

    if (quantizeMode == 1 || detectedBpm <= 0)
    {
        // MODE 1: OFF - markers at exact beat positions (no stretching)
        markers.reserve(beatTimes.size());
        for (float bt : beatTimes)
        {
            double pos = static_cast<double>(bt) + takeOffset;
            markers.push_back({pos, pos});
        }
    }
    else if (quantizeMode == 2)
    {
        // MODE 2: STRAIGHT - mathematical grid from detected BPM.
        // dst = firstBeat + n * beatInterval. One continuous grid.
        // Best for modern produced music with constant BPM.
        // Uses sequential gap rounding (local, no drift).

        double beatInterval = 60.0 / static_cast<double>(detectedBpm);
        auto [filtered, gridIdx] = buildGridIndices(beatInterval);
        double firstBeat = static_cast<double>(filtered[0]);

        for (size_t i = 0; i < filtered.size(); ++i)
        {
            double src = static_cast<double>(filtered[i]) + takeOffset;
            double dst = firstBeat + gridIdx[i] * beatInterval + takeOffset;
            markers.push_back({src, dst});
        }
    }
    else if (quantizeMode == 3 && downbeats.size() >= 2 && timeSigNum > 0)
    {
        // MODE 3: BARS - downbeat subdivision with internal grid.
        // Downbeats are anchors; beats evenly divided between them.
        // Each bar can have different duration. Good for live/variable tempo.

        for (size_t i = 0; i + 1 < downbeats.size(); ++i)
        {
            double barStart = static_cast<double>(downbeats[i]);
            double barEnd = static_cast<double>(downbeats[i + 1]);
            double barDur = barEnd - barStart;

            for (int b = 0; b < timeSigNum; ++b)
            {
                double target = barStart + b * barDur / timeSigNum;
                double nearest = findNearestBeat(target);

                markers.push_back({nearest + takeOffset, target + takeOffset});
            }
        }
        // Last downbeat
        double last = static_cast<double>(downbeats.back());
        markers.push_back({last + takeOffset, last + takeOffset});
    }
    else if (quantizeMode == 4 && downbeats.size() >= 2 && timeSigNum > 0)
    {
        // MODE 4: PROJECT GRID - downbeat subdivision + REAPER grid snap.
        // Same structure as Bars, but target positions are snapped to
        // REAPER's project grid. Both tracks snap to the same grid -> sync.

        for (size_t i = 0; i + 1 < downbeats.size(); ++i)
        {
            double barStart = static_cast<double>(downbeats[i]);
            double barEnd = static_cast<double>(downbeats[i + 1]);
            double barDur = barEnd - barStart;

            for (int b = 0; b < timeSigNum; ++b)
            {
                double target = barStart + b * barDur / timeSigNum;
                double nearest = findNearestBeat(target);

                // Convert target to timeline and snap to REAPER grid
                double timeline = itemPos + target / playrate;
                double gridTime = snapToGrid(timeline);
                double dst = (gridTime - itemPos) * playrate + takeOffset;

                markers.push_back({nearest + takeOffset, dst});
            }
        }
        double last = static_cast<double>(downbeats.back());
        markers.push_back({last + takeOffset, last + takeOffset});
    }
    else
    {
        // FALLBACK: modes 3/4 without downbeats -> sequential gap rounding
        double beatInterval = 60.0 / static_cast<double>(detectedBpm);
        auto [filtered, gridIdx] = buildGridIndices(beatInterval);
        double firstBeat = static_cast<double>(filtered[0]);

        if (quantizeMode == 4)
        {
            // Project grid fallback: snap sequential grid to REAPER grid
            for (size_t i = 0; i < filtered.size(); ++i)
            {
                double src = static_cast<double>(filtered[i]) + takeOffset;
                double timeline = itemPos + static_cast<double>(filtered[i]) / playrate;
                double gridTime = snapToGrid(timeline);
                double dst = (gridTime - itemPos) * playrate + takeOffset;
                markers.push_back({src, dst});
            }
        }
        else
        {
            // Bars fallback: mathematical grid (same as Straight)
            for (size_t i = 0; i < filtered.size(); ++i)
            {
                double src = static_cast<double>(filtered[i]) + takeOffset;
                double dst = firstBeat + gridIdx[i] * beatInterval + takeOffset;
                markers.push_back({src, dst});
            }
        }
    }

    // Apply quantize strength: interpolate dst between src (no change) and grid dst.
    // strength=1.0 = full quantize (current behavior), 0.0 = no stretching.
    if (strength < 1.0f && quantizeMode > 1)
    {
        double s = static_cast<double>(std::clamp(strength, 0.0f, 1.0f));
        for (auto& m : markers)
            m.dst = m.src + s * (m.dst - m.src);
    }

    // Deduplicate: prevent two markers from sharing the same source position.
    // This happens when findNearestBeat matches the same beat to multiple grid
    // positions (e.g., after user adds a beat near an existing one).
    // Two markers with the same src create a 0.00x ratio - disastrous.
    for (size_t i = 1; i < markers.size(); ++i)
    {
        if (std::abs(markers[i].src - markers[i - 1].src) < 0.005)
        {
            // Two markers want the same source beat - the worse match gets src=dst
            double distPrev = std::abs(markers[i - 1].src - markers[i - 1].dst);
            double distCurr = std::abs(markers[i].src - markers[i].dst);
            if (distCurr > distPrev)
                markers[i].src = markers[i].dst;
            else
                markers[i - 1].src = markers[i - 1].dst;
        }
    }

    int count = 0;
    for (const auto& m : markers)
    {
        int idx = SetTakeStretchMarker(take, -1, m.dst, &m.src);
        if (idx >= 0) ++count;
    }

    // Apply stretch algorithm flag
    if (stretchFlag > 0 && count > 0)
        SetMediaItemTakeInfo_Value(take, "I_STRETCHFLAGS", stretchFlag);

    UpdateItemInProject(item);

    static const char* modeLabel[] = {"", " (straight)", " (bars)", " (project grid)"};
    char label[64];
    snprintf(label, sizeof(label), "ReaBeat: Insert %d stretch markers%s",
             count, (quantizeMode >= 2 && quantizeMode <= 4) ? modeLabel[quantizeMode - 1] : "");
    Undo_EndBlock2(nullptr, label, -1);

    return count;
}

// --- Insert Tempo Map ---

int ReaperActions::insertTempoMap(
    MediaItem_Take* take,
    MediaItem* item,
    float tempo,
    const std::vector<float>& beatList,
    int beatsPerMarker,
    int timeSigNum,
    int timeSigDenom,
    const std::string& mode)
{
    if (!take || !item || beatList.empty())
        return 0;

    Undo_BeginBlock2(nullptr);
    RefreshGuard refreshGuard;

    // Clear existing tempo markers (always replace - Ctrl+Z to undo)
    int existing = CountTempoTimeSigMarkers(nullptr);
    if (existing > 0)
    {
        for (int i = existing - 1; i >= 0; --i)
            DeleteTempoTimeSigMarker(nullptr, i);
    }

    double itemPos = GetMediaItemInfo_Value(item, "D_POSITION");
    double takeOffset = GetMediaItemTakeInfo_Value(take, "D_STARTOFFS");
    double playrate = GetMediaItemTakeInfo_Value(take, "D_PLAYRATE");
    double effectiveBpm = tempo * playrate;

    int count = 0;

    if (mode != "constant" && beatList.size() >= 2)
    {
        // Variable tempo: one marker per beat/downbeat position
        for (size_t i = 0; i < beatList.size() - 1; ++i)
        {
            double pos = itemPos + (beatList[i] - takeOffset) / playrate;
            double nextPos = itemPos + (beatList[i + 1] - takeOffset) / playrate;
            double interval = nextPos - pos;

            if (interval < 0.05) continue;  // skip tiny intervals

            double localBpm = 60.0 * beatsPerMarker / interval;

            // Octave correction
            while (localBpm < 78.0) localBpm *= 2.0;
            while (localBpm > 185.0) localBpm /= 2.0;

            // Filter: within 25% of detected tempo
            double ratio = localBpm / effectiveBpm;
            if (ratio > 0.75 && ratio < 1.25)
            {
                SetTempoTimeSigMarker(nullptr, -1, pos, -1, -1,
                    localBpm, timeSigNum, timeSigDenom, false);
                ++count;
            }
        }
    }
    else
    {
        // Constant tempo: single marker at bar-snapped position
        double firstPos = itemPos + (beatList[0] - takeOffset) / playrate;
        double snapTo = snapToNearestBar(firstPos);

        SetTempoTimeSigMarker(nullptr, -1, snapTo, -1, -1,
            effectiveBpm, timeSigNum, timeSigDenom, false);
        count = 1;
    }

    UpdateTimeline();

    char label[64];
    snprintf(label, sizeof(label), "ReaBeat: Insert %d tempo marker(s)", count);
    Undo_EndBlock2(nullptr, label, -1);

    return count;
}

// --- Match Tempo ---

bool ReaperActions::matchTempo(
    MediaItem_Take* take,
    MediaItem* item,
    float detectedBpm,
    float targetBpm,
    float firstDownbeatSrc)
{
    if (!take || !item || detectedBpm <= 0 || targetBpm <= 0)
        return false;

    double rate = targetBpm / detectedBpm;

    if (rate < 0.25 || rate > 4.0)
    {
        char msg[256];
        snprintf(msg, sizeof(msg),
            "Tempo ratio too extreme: %.1f BPM -> %.1f BPM (%.1fx)\n\n"
            "Supported range: 0.25x to 4.0x",
            detectedBpm, targetBpm, rate);
        ShowMessageBox(msg, "ReaBeat - Match Tempo", 0);
        return false;
    }

    Undo_BeginBlock2(nullptr);
    RefreshGuard refreshGuard;

    // Set playrate and preserve pitch
    SetMediaItemTakeInfo_Value(take, "D_PLAYRATE", rate);
    SetMediaItemTakeInfo_Value(take, "B_PPITCH", 1.0);

    // Adjust item length
    PCM_source* source = GetMediaItemTake_Source(take);
    if (source)
    {
        double sourceLen = 0;
        bool lengthIsQN = false;
        sourceLen = GetMediaSourceLength(source, &lengthIsQN);
        double offset = GetMediaItemTakeInfo_Value(take, "D_STARTOFFS");
        double newLen = (sourceLen - offset) / rate;
        SetMediaItemInfo_Value(item, "D_LENGTH", newLen);
    }

    // Auto-align first downbeat to nearest bar
    if (firstDownbeatSrc >= 0)
    {
        double itemPos = GetMediaItemInfo_Value(item, "D_POSITION");
        double takeOffset = GetMediaItemTakeInfo_Value(take, "D_STARTOFFS");

        // Where first downbeat plays after rate change
        double downbeatTimeline = itemPos + (firstDownbeatSrc - takeOffset) / rate;

        double snapTo = snapToNearestBar(downbeatTimeline);

        // Shift item
        double shift = snapTo - downbeatTimeline;
        SetMediaItemInfo_Value(item, "D_POSITION", itemPos + shift);
    }

    UpdateItemInProject(item);

    char label[64];
    snprintf(label, sizeof(label), "ReaBeat: Match tempo %.1f -> %.1f BPM", detectedBpm, targetBpm);
    Undo_EndBlock2(nullptr, label, -1);

    return true;
}

// --- Single Stretch Marker Operations (marker edit mode) ---

static constexpr double kSrcPosTolerance = 0.005; // 5ms

double ReaperActions::interpolateDst(MediaItem_Take* take, double newSrc)
{
    int n = GetTakeNumStretchMarkers(take);
    if (n < 2) return newSrc;

    double prevSrc = -1e9, prevDst = -1e9;
    double nextSrc = 1e9, nextDst = 1e9;

    for (int i = 0; i < n; ++i)
    {
        double pos, srcpos;
        GetTakeStretchMarker(take, i, &pos, &srcpos);
        if (srcpos <= newSrc) { prevSrc = srcpos; prevDst = pos; }
        if (srcpos > newSrc && nextSrc > 1e8) { nextSrc = srcpos; nextDst = pos; break; }
    }

    if (prevSrc > -1e8 && nextSrc < 1e8)
    {
        double span = nextSrc - prevSrc;
        if (span > 0.001)
        {
            double frac = (newSrc - prevSrc) / span;
            return prevDst + frac * (nextDst - prevDst);
        }
    }

    // Edge: before first or after last - extrapolate with same offset
    if (prevSrc > -1e8) return prevDst + (newSrc - prevSrc);
    if (nextSrc < 1e8) return nextDst - (nextSrc - newSrc);

    return newSrc;
}

int ReaperActions::addOneStretchMarker(MediaItem_Take* take, MediaItem* item,
                                        double src, double dst)
{
    if (!take || !item) return -1;

    Undo_BeginBlock2(nullptr);
    RefreshGuard refreshGuard;

    int idx = SetTakeStretchMarker(take, -1, dst, &src);

    UpdateItemInProject(item);
    Undo_EndBlock2(nullptr, "ReaBeat: Add stretch marker", -1);

    return idx;
}

bool ReaperActions::moveOneStretchMarker(MediaItem_Take* take, MediaItem* item,
                                          double oldSrc, double newSrc)
{
    if (!take || !item) return false;

    // Find marker by source position
    int n = GetTakeNumStretchMarkers(take);
    int targetIdx = -1;
    double oldDst = 0;

    for (int i = 0; i < n; ++i)
    {
        double pos, srcpos;
        GetTakeStretchMarker(take, i, &pos, &srcpos);
        if (std::abs(srcpos - oldSrc) < kSrcPosTolerance)
        {
            targetIdx = i;
            oldDst = pos;
            break;
        }
    }

    if (targetIdx < 0) return false;

    // Shift dst by same amount as src (preserves stretch offset)
    double newDst = oldDst + (newSrc - oldSrc);

    Undo_BeginBlock2(nullptr);
    RefreshGuard refreshGuard;

    SetTakeStretchMarker(take, targetIdx, newDst, &newSrc);

    UpdateItemInProject(item);
    Undo_EndBlock2(nullptr, "ReaBeat: Move stretch marker", -1);

    return true;
}

bool ReaperActions::deleteOneStretchMarker(MediaItem_Take* take, MediaItem* item,
                                            double src)
{
    if (!take || !item) return false;

    int n = GetTakeNumStretchMarkers(take);
    int targetIdx = -1;

    for (int i = 0; i < n; ++i)
    {
        double pos, srcpos;
        GetTakeStretchMarker(take, i, &pos, &srcpos);
        if (std::abs(srcpos - src) < kSrcPosTolerance)
        {
            targetIdx = i;
            break;
        }
    }

    if (targetIdx < 0) return false;

    Undo_BeginBlock2(nullptr);
    RefreshGuard refreshGuard;

    int one = 1;
    DeleteTakeStretchMarkers(take, targetIdx, &one);

    UpdateItemInProject(item);
    Undo_EndBlock2(nullptr, "ReaBeat: Delete stretch marker", -1);

    return true;
}

// --- Get Project BPM ---

float ReaperActions::getProjectBpm()
{
    double bpm = 0;
    double bpi = 0;
    GetProjectTimeSignature2(nullptr, &bpm, &bpi);
    return static_cast<float>(bpm);
}

void ReaperActions::setProjectBpm(float bpm)
{
    if (bpm <= 0) return;

    Undo_BeginBlock2(nullptr);
    SetCurrentBPM(nullptr, bpm, true);
    UpdateTimeline();
    Undo_EndBlock2(nullptr, "ReaBeat: Set session tempo", -1);
}
