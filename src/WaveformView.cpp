#include "WaveformView.h"
#include "reaper_plugin.h"
#include "reaper_plugin_functions.h"
#include <algorithm>
#include <cmath>

// Layout
static constexpr float kRulerH = 16.0f;
static constexpr float kScrollH = 3.0f;
static constexpr float kBeatHitPx = 8.0f;
static constexpr double kMinViewSec = 0.5;

// Theme - matches ReaBeat dark + gold
namespace WC {
    static const juce::Colour bg         {0xff141414};
    static const juce::Colour bgRound    {0xff1a1a1a};
    static const juce::Colour waveFill   {0xff454545};
    static const juce::Colour waveEdge   {0xff585858};
    static const juce::Colour center     {0x14ffffff};
    static const juce::Colour gold       {0xffc8a040};
    static const juce::Colour goldDim    {0x30c8a040};
    static const juce::Colour goldMid    {0x60c8a040};
    static const juce::Colour goldBright {0xb0c8a040};
    static const juce::Colour goldGhost  {0x18c8a040};
    static const juce::Colour ruleDiv    {0xff2a2a2a};
    static const juce::Colour ruleTick   {0xff3a3a3a};
    static const juce::Colour ruleText   {0xff5a5a5a};
    static const juce::Colour noData     {0xff383838};
    static const juce::Colour playhead   {0xddffffff};
    static const juce::Colour scrollTrack{0xff1e1e1e};
    static const juce::Colour scrollThumb{0x50c8a040};
}

// ============================================================
//  Construction / data
// ============================================================

WaveformView::WaveformView()
{
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
    setWantsKeyboardFocus(true);
}

void WaveformView::setData(const std::vector<float>& peaks,
                            const std::vector<float>& beats,
                            const std::vector<float>& downbeats,
                            float duration)
{
    peaks_ = peaks;
    beats_ = beats;
    downbeats_ = downbeats;
    duration_ = duration;
    hasData_ = !peaks.empty() && duration > 0;

    viewStart_ = 0.0;
    viewDuration_ = duration;
    lastBeatIdx_ = -1;
    hoveredBeatIdx_ = -1;
    dragBeatIdx_ = -1;
    beatsEdited_ = false;
    markerEditMode_ = false;
    hoveredMarkerIdx_ = -1;
    dragMarkerIdx_ = -1;
    undoStack_.clear();
    redoStack_.clear();
    repaint();
}

void WaveformView::setItemInfo(double itemPos, double takeOffset, double playrate)
{
    itemPos_ = itemPos;
    takeOffset_ = takeOffset;
    playrate_ = (playrate > 0) ? playrate : 1.0;
    startTimer(50);
}

void WaveformView::setReaperMarkers(const std::vector<float>& srcPositions,
                                     const std::vector<std::pair<double,double>>& pairs)
{
    reaperMarkers_ = srcPositions;
    stretchPairs_.clear();
    stretchPairs_.reserve(pairs.size());
    for (auto& [src, dst] : pairs)
        stretchPairs_.push_back({src, dst});
    repaint();
}

void WaveformView::clear()
{
    peaks_.clear();
    beats_.clear();
    downbeats_.clear();
    reaperMarkers_.clear();
    stretchPairs_.clear();
    markerEditMode_ = false;
    hoveredMarkerIdx_ = -1;
    dragMarkerIdx_ = -1;
    duration_ = 0;
    hasData_ = false;
    playheadPos_ = -1;
    lastBeatIdx_ = -1;
    hoveredBeatIdx_ = -1;
    dragBeatIdx_ = -1;
    beatsEdited_ = false;
    undoStack_.clear();
    redoStack_.clear();
    viewStart_ = 0;
    viewDuration_ = 0;
    stopTimer();
    repaint();
}

void WaveformView::zoomToFit()
{
    if (duration_ > 0)
    {
        viewStart_ = 0.0;
        viewDuration_ = duration_;
        repaint();
    }
}

// ============================================================
//  Coordinate helpers
// ============================================================

float WaveformView::timeToX(double time) const
{
    if (viewDuration_ <= 0) return 0.0f;
    return static_cast<float>((time - viewStart_) / viewDuration_ * getWidth());
}

double WaveformView::xToTime(float x) const
{
    if (getWidth() <= 0) return viewStart_;
    return viewStart_ + static_cast<double>(x) / getWidth() * viewDuration_;
}

int WaveformView::findNearestBeat(float x, float maxDistPx) const
{
    int nearest = -1;
    float best = maxDistPx;
    for (int i = 0; i < static_cast<int>(beats_.size()); ++i)
    {
        float bx = timeToX(beats_[i]);
        float d = std::abs(bx - x);
        if (d < best) { best = d; nearest = i; }
    }
    return nearest;
}

bool WaveformView::isBeatDownbeat(int beatIdx) const
{
    if (beatIdx < 0 || beatIdx >= static_cast<int>(beats_.size()))
        return false;
    float bt = beats_[beatIdx];
    for (float db : downbeats_)
    {
        if (std::abs(db - bt) < 0.025f) return true;
        if (db > bt + 0.025f) break;
    }
    return false;
}

void WaveformView::notifyBeatsEdited()
{
    beatsEdited_ = true;
    if (onBeatsEdited) onBeatsEdited(beats_, downbeats_);
}

int WaveformView::findNearestReaperMarker(float x, float maxDistPx) const
{
    int nearest = -1;
    float best = maxDistPx;
    for (int i = 0; i < static_cast<int>(reaperMarkers_.size()); ++i)
    {
        float mx = timeToX(reaperMarkers_[i]);
        float d = std::abs(mx - x);
        if (d < best) { best = d; nearest = i; }
    }
    return nearest;
}

void WaveformView::setMarkerEditMode(bool enabled)
{
    markerEditMode_ = enabled;
    hoveredBeatIdx_ = -1;
    hoveredMarkerIdx_ = -1;
    dragBeatIdx_ = -1;
    dragMarkerIdx_ = -1;
    potentialDragIdx_ = -1;
    repaint();
}

bool WaveformView::scrollToNextGap()
{
    if (beats_.size() < 4) return false;

    // Compute median beat interval
    std::vector<float> intervals;
    intervals.reserve(beats_.size());
    for (size_t i = 1; i < beats_.size(); ++i)
        intervals.push_back(beats_[i] - beats_[i - 1]);
    std::sort(intervals.begin(), intervals.end());
    float median = intervals[intervals.size() / 2];
    float thresh = median * 1.35f;

    // Find next gap after current view center
    float viewCenter = static_cast<float>(viewStart_ + viewDuration_ * 0.5);
    int startIdx = -1;

    // First pass: find gaps after view center
    for (size_t i = 1; i < beats_.size(); ++i)
    {
        if (beats_[i] <= viewCenter) continue;
        if (beats_[i] - beats_[i - 1] > thresh)
        { startIdx = static_cast<int>(i); break; }
    }

    // Wrap around: search from beginning
    if (startIdx < 0)
    {
        for (size_t i = 1; i < beats_.size(); ++i)
        {
            if (beats_[i] - beats_[i - 1] > thresh)
            { startIdx = static_cast<int>(i); break; }
        }
    }

    if (startIdx < 0) return false;

    // Zoom to show gap with 1 beat context on each side
    float gapStart = beats_[startIdx - 1];
    float gapEnd = beats_[startIdx];
    float context = median;
    viewStart_ = std::max(0.0, static_cast<double>(gapStart - context));
    viewDuration_ = static_cast<double>(gapEnd + context) - viewStart_;
    viewDuration_ = std::min(viewDuration_, static_cast<double>(duration_));
    followPlayhead_ = false;
    repaint();
    return true;
}

void WaveformView::triggerFlash(float position)
{
    flashPosition_ = position;
    flashTimestamp_ = juce::Time::getMillisecondCounter();
    repaint();
}

void WaveformView::setGridBpm(float bpm, float firstBeat)
{
    gridBpm_ = bpm;
    gridFirstBeat_ = firstBeat;
}

float WaveformView::snapToPeak(float time, float windowSec) const
{
    if (peaks_.empty() || duration_ <= 0) return time;

    float peaksPerSec = static_cast<float>(peaks_.size()) / duration_;
    int center = static_cast<int>(time * peaksPerSec);
    int range = static_cast<int>(windowSec * peaksPerSec);
    int best = center;
    float bestVal = 0;

    for (int i = std::max(0, center - range);
         i <= std::min(static_cast<int>(peaks_.size()) - 1, center + range); ++i)
    {
        if (peaks_[i] > bestVal)
        {
            bestVal = peaks_[i];
            best = i;
        }
    }
    return static_cast<float>(best) / peaksPerSec;
}

double WaveformView::srcTimeToTimeline(double srcTime) const
{
    // Convert source audio time to REAPER timeline position.
    // Without stretch markers: linear formula.
    // With stretch markers: snap to nearest marker or interpolate.

    if (stretchPairs_.size() >= 2)
    {
        double s = srcTime + takeOffset_;

        // Snap to nearest stretch marker if close.
        // Tolerance = half the minimum marker spacing (always catches nearest
        // without ambiguity, even when neural beats differ from grid positions).
        double snapDist = 0.25; // fallback
        {
            double minSpacing = 999;
            for (size_t i = 1; i < stretchPairs_.size(); ++i)
            {
                double d = stretchPairs_[i].src - stretchPairs_[i - 1].src;
                if (d > 0.01 && d < minSpacing) minSpacing = d;
            }
            if (minSpacing < 999) snapDist = minSpacing * 0.5;
        }
        double bestDist = snapDist;
        double bestDst = -1;
        for (auto& sp : stretchPairs_)
        {
            double d = std::abs(sp.src - s);
            if (d < bestDist)
            {
                bestDist = d;
                bestDst = sp.dst;
            }
            if (sp.src > s + snapDist) break; // sorted, no need to check further
        }

        if (bestDst >= 0)
            return itemPos_ + (bestDst - takeOffset_) / playrate_;

        // Interpolate between markers for positions not near any marker
        if (s <= stretchPairs_.front().src)
        {
            double dst = stretchPairs_.front().dst;
            return itemPos_ + (dst - takeOffset_) / playrate_;
        }

        if (s >= stretchPairs_.back().src)
        {
            double dst = stretchPairs_.back().dst;
            return itemPos_ + (dst - takeOffset_) / playrate_;
        }

        for (size_t i = 0; i + 1 < stretchPairs_.size(); ++i)
        {
            if (s >= stretchPairs_[i].src && s < stretchPairs_[i + 1].src)
            {
                double srcSpan = stretchPairs_[i + 1].src - stretchPairs_[i].src;
                double frac = (srcSpan > 0) ? (s - stretchPairs_[i].src) / srcSpan : 0;
                double dstSpan = stretchPairs_[i + 1].dst - stretchPairs_[i].dst;
                double dst = stretchPairs_[i].dst + frac * dstSpan;
                return itemPos_ + (dst - takeOffset_) / playrate_;
            }
        }
    }

    // Fallback: linear
    return itemPos_ + (srcTime - takeOffset_) / playrate_;
}

void WaveformView::pushUndoState()
{
    undoStack_.push_back({beats_, downbeats_});
    if (static_cast<int>(undoStack_.size()) > kMaxUndo)
        undoStack_.erase(undoStack_.begin());
    redoStack_.clear();
}

bool WaveformView::keyPressed(const juce::KeyPress& key)
{
    // Space = REAPER play/stop (works in both modes)
    if (key.getKeyCode() == juce::KeyPress::spaceKey)
    {
        if (Main_OnCommand)
            Main_OnCommand(40044, 0);  // Transport: Play/Stop
        return true;
    }

    // Enter = Apply action
    if (key.getKeyCode() == juce::KeyPress::returnKey)
    {
        if (onApplyRequested)
            onApplyRequested();
        return true;
    }

    // N = scroll to next gap
    if (key.getKeyCode() == 'N' && !key.getModifiers().isCommandDown())
    {
        scrollToNextGap();
        return true;
    }

    // In marker mode: delegate undo/redo to REAPER
    if (markerEditMode_)
    {
        bool cmd = key.getModifiers().isCommandDown();
        if (cmd && key.getKeyCode() == 'Z')
        {
            bool shift = key.getModifiers().isShiftDown();
            if (Main_OnCommand)
                Main_OnCommand(shift ? 40030 : 40029, 0);
            return true;
        }
        return false;
    }

    // Cmd+Z = undo, Cmd+Shift+Z = redo
    bool cmd = key.getModifiers().isCommandDown();
    bool shift = key.getModifiers().isShiftDown();

    if (cmd && key.getKeyCode() == 'Z' && !shift)
    {
        if (!undoStack_.empty())
        {
            redoStack_.push_back({beats_, downbeats_});
            auto snap = std::move(undoStack_.back());
            undoStack_.pop_back();
            beats_ = std::move(snap.beats);
            downbeats_ = std::move(snap.downbeats);
            notifyBeatsEdited();
            repaint();
        }
        return true;
    }

    if (cmd && key.getKeyCode() == 'Z' && shift)
    {
        if (!redoStack_.empty())
        {
            undoStack_.push_back({beats_, downbeats_});
            auto snap = std::move(redoStack_.back());
            redoStack_.pop_back();
            beats_ = std::move(snap.beats);
            downbeats_ = std::move(snap.downbeats);
            notifyBeatsEdited();
            repaint();
        }
        return true;
    }

    return false;
}

// ============================================================
//  Paint
// ============================================================

void WaveformView::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    float w = bounds.getWidth();
    float fullH = bounds.getHeight();

    bool isZoomed = hasData_ && (viewDuration_ < duration_ - 0.01);
    float scrollH = isZoomed ? kScrollH : 0.0f;
    float waveH = fullH - kRulerH - scrollH;
    float cx = waveH * 0.5f;

    // Background
    g.setColour(WC::bgRound);
    g.fillRoundedRectangle(bounds, 4.0f);

    if (!hasData_ || w < 2.0f || viewDuration_ <= 0)
    {
        g.setColour(WC::noData);
        g.setFont(juce::FontOptions(11.0f));
        g.drawText("No waveform data", bounds, juce::Justification::centred);
        return;
    }

    float peaksPerSec = static_cast<float>(peaks_.size()) / duration_;
    int peakSize = static_cast<int>(peaks_.size());
    float peaksPerPx = peaksPerSec * static_cast<float>(viewDuration_) / w;

    // ---- Waveform ----
    {
        juce::Path waveTop, waveBot;
        waveTop.startNewSubPath(0.0f, cx);
        waveBot.startNewSubPath(0.0f, cx);

        for (int px = 0; px < static_cast<int>(w); ++px)
        {
            double t = viewStart_ + (static_cast<double>(px) / w) * viewDuration_;
            float peakF = static_cast<float>(t * peaksPerSec);
            float val;

            if (peaksPerPx >= 2.0f)
            {
                // Zoomed out - take max in range so peaks aren't lost
                int center = static_cast<int>(peakF);
                int range = std::max(1, static_cast<int>(peaksPerPx * 0.5f));
                float maxV = 0.0f;
                for (int r = -range; r <= range; ++r)
                {
                    int idx = center + r;
                    if (idx >= 0 && idx < peakSize)
                        maxV = std::max(maxV, peaks_[idx]);
                }
                val = maxV;
            }
            else
            {
                // Zoomed in - linear interpolation for smooth curve
                int i0 = std::clamp(static_cast<int>(peakF), 0, peakSize - 1);
                int i1 = std::clamp(i0 + 1, 0, peakSize - 1);
                float frac = peakF - static_cast<float>(i0);
                val = peaks_[i0] * (1.0f - frac) + peaks_[i1] * frac;
            }

            float amp = val * (waveH * 0.44f);
            waveTop.lineTo(static_cast<float>(px), cx - amp);
            waveBot.lineTo(static_cast<float>(px), cx + amp);
        }

        waveTop.lineTo(w, cx);  waveTop.closeSubPath();
        waveBot.lineTo(w, cx);  waveBot.closeSubPath();

        g.setColour(WC::waveFill);
        g.fillPath(waveTop);
        g.fillPath(waveBot);

        g.setColour(WC::waveEdge);
        g.strokePath(waveTop, juce::PathStrokeType(0.5f));
        g.strokePath(waveBot, juce::PathStrokeType(0.5f));
    }

    // Center line
    g.setColour(WC::center);
    g.drawHorizontalLine(static_cast<int>(cx), 0.0f, w);

    // In marker edit mode, draw beats as dim reference + grid overlay
    if (markerEditMode_)
    {
        // Dim neural beat reference lines
        auto visStart = std::lower_bound(beats_.begin(), beats_.end(),
                                         static_cast<float>(viewStart_ - 0.01));
        auto visEnd = std::upper_bound(beats_.begin(), beats_.end(),
                                       static_cast<float>(viewStart_ + viewDuration_ + 0.01));
        g.setColour(juce::Colour(0x18808080));
        for (auto it = visStart; it != visEnd; ++it)
        {
            float bx = timeToX(*it);
            g.drawVerticalLine(static_cast<int>(bx), 8.0f, waveH - 8.0f);
        }

        // Regular grid overlay (expected beat positions from BPM)
        if (gridBpm_ > 0 && gridFirstBeat_ >= 0)
        {
            float beatInterval = 60.0f / gridBpm_;
            float tStart = static_cast<float>(viewStart_);
            float tEnd = static_cast<float>(viewStart_ + viewDuration_);

            // Find first grid beat in view
            int nStart = std::max(0, static_cast<int>(std::ceil((tStart - gridFirstBeat_) / beatInterval)));
            int nEnd = static_cast<int>(std::floor((tEnd - gridFirstBeat_) / beatInterval));

            g.setColour(juce::Colour(0x10409dc8));  // very dim teal
            for (int n = nStart; n <= nEnd; ++n)
            {
                float t = gridFirstBeat_ + static_cast<float>(n) * beatInterval;
                float gx = timeToX(t);
                if (gx >= 0 && gx < w)
                {
                    float dash = 2.0f;
                    for (float dy = 8.0f; dy < waveH - 8.0f; dy += dash * 3)
                        g.drawVerticalLine(static_cast<int>(gx), dy, std::min(dy + dash, waveH - 8.0f));
                }
            }
        }
    }

    // ---- Gap highlighting (missing beats) ----
    // Highlight intervals > 1.35x median as potential problems
    if (!markerEditMode_ && beats_.size() >= 4)
    {
        // Compute median beat interval
        std::vector<float> intervals;
        intervals.reserve(beats_.size());
        for (size_t i = 1; i < beats_.size(); ++i)
            intervals.push_back(beats_[i] - beats_[i - 1]);

        std::sort(intervals.begin(), intervals.end());
        float median = intervals[intervals.size() / 2];
        float gapThresh = median * 1.35f;
        float peaksPerSec = peaks_.empty() ? 0 : static_cast<float>(peaks_.size()) / duration_;

        for (size_t i = 1; i < beats_.size(); ++i)
        {
            float gap = beats_[i] - beats_[i - 1];
            if (gap <= gapThresh) continue;

            float x1 = timeToX(beats_[i - 1]);
            float x2 = timeToX(beats_[i]);
            if (x2 < 0 || x1 > w) continue;

            x1 = std::max(x1, 0.0f);
            x2 = std::min(x2, w);

            // Red-orange tint over gap area
            g.setColour(juce::Colour(0x18d94848));
            g.fillRect(x1, 0.0f, x2 - x1, waveH);

            // Suggest where missing beats should be (at RMS peak positions)
            int nMissing = std::max(1, static_cast<int>(std::round(gap / median)) - 1);
            float spacing = gap / static_cast<float>(nMissing + 1);

            g.setColour(juce::Colour(0x40d94848));
            float dash = 3.0f;

            for (int m = 1; m <= nMissing; ++m)
            {
                float expectedTime = beats_[i - 1] + spacing * static_cast<float>(m);
                float sugTime = expectedTime;

                // Snap suggestion to strongest RMS peak nearby (+/- 40% of spacing)
                if (peaksPerSec > 0)
                {
                    float searchWindow = spacing * 0.4f;
                    int pStart = std::max(0, static_cast<int>((expectedTime - searchWindow) * peaksPerSec));
                    int pEnd = std::min(static_cast<int>(peaks_.size()) - 1,
                                        static_cast<int>((expectedTime + searchWindow) * peaksPerSec));
                    float bestPeak = 0;
                    for (int p = pStart; p <= pEnd; ++p)
                    {
                        if (peaks_[p] > bestPeak)
                        {
                            bestPeak = peaks_[p];
                            sugTime = static_cast<float>(p) / peaksPerSec;
                        }
                    }
                }

                float sugX = timeToX(sugTime);
                if (sugX >= 0 && sugX < w)
                {
                    for (float dy = 8.0f; dy < waveH - 8.0f; dy += dash * 2)
                        g.drawVerticalLine(static_cast<int>(sugX), dy,
                                           std::min(dy + dash, waveH - 8.0f));
                }
            }
        }
    }

    // ---- Beat markers (beat mode only - in marker mode, drawn as dim reference above) ----
    if (!markerEditMode_)
    {
    auto visStart = std::lower_bound(beats_.begin(), beats_.end(),
                                     static_cast<float>(viewStart_ - 0.01));
    auto visEnd = std::upper_bound(beats_.begin(), beats_.end(),
                                   static_cast<float>(viewStart_ + viewDuration_ + 0.01));
    int visibleBeats = static_cast<int>(std::distance(visStart, visEnd));
    float avgBeatSpacing = visibleBeats > 1 ? w / static_cast<float>(visibleBeats) : w;
    bool showBeats = avgBeatSpacing >= 3.0f;

    if (showBeats)
    {
        for (auto it = visStart; it != visEnd; ++it)
        {
            int i = static_cast<int>(std::distance(beats_.begin(), it));
            if (isBeatDownbeat(i)) continue;  // drawn with downbeats

            float bx = timeToX(beats_[i]);
            int ix = static_cast<int>(bx);

            if (i == hoveredBeatIdx_)
            {
                // Glow halo
                g.setColour(juce::Colour(0x0cc8a040));
                g.drawVerticalLine(ix - 3, 6.0f, waveH - 6.0f);
                g.drawVerticalLine(ix + 3, 6.0f, waveH - 6.0f);
                g.setColour(juce::Colour(0x1cc8a040));
                g.drawVerticalLine(ix - 2, 4.0f, waveH - 4.0f);
                g.drawVerticalLine(ix + 2, 4.0f, waveH - 4.0f);
                g.setColour(juce::Colour(0x38c8a040));
                g.drawVerticalLine(ix - 1, 3.0f, waveH - 3.0f);
                g.drawVerticalLine(ix + 1, 3.0f, waveH - 3.0f);
                g.setColour(WC::gold);
                g.drawVerticalLine(ix, 2.0f, waveH - 2.0f);
                // Handle dot
                g.fillEllipse(bx - 3.0f, 2.0f, 6.0f, 6.0f);
            }
            else if (i == dragBeatIdx_)
            {
                g.setColour(WC::gold);
                g.drawVerticalLine(ix, 2.0f, waveH - 2.0f);
                g.fillEllipse(bx - 3.0f, 2.0f, 6.0f, 6.0f);
            }
            else
            {
                g.setColour(WC::goldDim);
                g.drawVerticalLine(ix, 6.0f, waveH - 6.0f);
            }
        }
    }

    // ---- Downbeat markers + bar numbers ----
    auto dbVisStart = std::lower_bound(downbeats_.begin(), downbeats_.end(),
                                       static_cast<float>(viewStart_ - 0.01));
    auto dbVisEnd = std::upper_bound(downbeats_.begin(), downbeats_.end(),
                                     static_cast<float>(viewStart_ + viewDuration_ + 0.01));
    int visibleDb = static_cast<int>(std::distance(dbVisStart, dbVisEnd));
    float avgDbSpacing = visibleDb > 1 ? w / static_cast<float>(visibleDb) : w;
    bool showBarNums = avgDbSpacing >= 16.0f;

    g.setFont(juce::FontOptions(9.0f));

    for (auto dit = dbVisStart; dit != dbVisEnd; ++dit)
    {
        size_t di = static_cast<size_t>(std::distance(downbeats_.begin(), dit));
        float dx = timeToX(*dit);
        int ix = static_cast<int>(dx);

        // Find corresponding beat index for hover check
        int beatIdx = -1;
        for (int bi = 0; bi < static_cast<int>(beats_.size()); ++bi)
        {
            if (std::abs(beats_[bi] - *dit) < 0.025f)
            { beatIdx = bi; break; }
        }

        bool hovered = (beatIdx >= 0 && beatIdx == hoveredBeatIdx_);
        bool dragged = (beatIdx >= 0 && beatIdx == dragBeatIdx_);

        if (hovered || dragged)
        {
            // Wide glow for downbeat
            g.setColour(juce::Colour(0x0ac8a040));
            for (int off = -5; off <= 5; ++off)
                if (off != 0) g.drawVerticalLine(ix + off, 1.0f, waveH - 1.0f);
            g.setColour(juce::Colour(0x30c8a040));
            g.drawVerticalLine(ix - 2, 1.0f, waveH - 1.0f);
            g.drawVerticalLine(ix + 2, 1.0f, waveH - 1.0f);
            g.setColour(WC::gold);
            g.drawVerticalLine(ix - 1, 1.0f, waveH - 1.0f);
            g.drawVerticalLine(ix,     1.0f, waveH - 1.0f);
            g.drawVerticalLine(ix + 1, 1.0f, waveH - 1.0f);
            // Larger handle
            g.fillEllipse(dx - 4.0f, 1.0f, 8.0f, 8.0f);
        }
        else
        {
            g.setColour(WC::goldBright);
            g.drawVerticalLine(ix,     1.0f, waveH - 1.0f);
            g.drawVerticalLine(ix + 1, 1.0f, waveH - 1.0f);
        }

        if (showBarNums)
        {
            g.setColour(juce::Colour(0x88c8a040));
            g.drawText(juce::String(static_cast<int>(di + 1)),
                       ix + 3, 1, 24, 11,
                       juce::Justification::centredLeft, false);
        }
    }
    } // end if (!markerEditMode_) - beat markers + downbeats

    // ---- REAPER stretch markers ----
    // In marker mode: gold primary (editable) with hover/drag effects
    // In beat mode: teal reference lines
    if (!reaperMarkers_.empty())
    {
        auto rmVisStart = std::lower_bound(reaperMarkers_.begin(), reaperMarkers_.end(),
                                           static_cast<float>(viewStart_ - 0.01));
        auto rmVisEnd = std::upper_bound(reaperMarkers_.begin(), reaperMarkers_.end(),
                                         static_cast<float>(viewStart_ + viewDuration_ + 0.01));

        for (auto it = rmVisStart; it != rmVisEnd; ++it)
        {
            int i = static_cast<int>(std::distance(reaperMarkers_.begin(), it));
            float mx = timeToX(*it);
            int ix = static_cast<int>(mx);
            if (ix < 0 || ix >= static_cast<int>(w)) continue;

            if (markerEditMode_)
            {
                // Gold primary markers with hover/drag effects
                if (i == hoveredMarkerIdx_ || i == dragMarkerIdx_)
                {
                    g.setColour(juce::Colour(0x0cc8a040));
                    g.drawVerticalLine(ix - 3, 6.0f, waveH - 6.0f);
                    g.drawVerticalLine(ix + 3, 6.0f, waveH - 6.0f);
                    g.setColour(juce::Colour(0x1cc8a040));
                    g.drawVerticalLine(ix - 2, 4.0f, waveH - 4.0f);
                    g.drawVerticalLine(ix + 2, 4.0f, waveH - 4.0f);
                    g.setColour(juce::Colour(0x38c8a040));
                    g.drawVerticalLine(ix - 1, 3.0f, waveH - 3.0f);
                    g.drawVerticalLine(ix + 1, 3.0f, waveH - 3.0f);
                    g.setColour(WC::gold);
                    g.drawVerticalLine(ix, 2.0f, waveH - 2.0f);
                    g.fillEllipse(mx - 3.0f, 2.0f, 6.0f, 6.0f);
                }
                else
                {
                    g.setColour(WC::goldDim);
                    g.drawVerticalLine(ix, 6.0f, waveH - 6.0f);
                }
            }
            else
            {
                // Teal reference lines (beat mode)
                bool matchesBeat = false;
                for (float bt : beats_)
                {
                    if (std::abs(bt - *it) < 0.025f) { matchesBeat = true; break; }
                    if (bt > *it + 0.025f) break;
                }

                if (!matchesBeat)
                {
                    g.setColour(juce::Colour(0x60409dc8));
                    g.drawVerticalLine(ix, 6.0f, waveH - 6.0f);
                }
                else
                {
                    g.setColour(juce::Colour(0x80409dc8));
                    g.fillRect(mx - 1.5f, 0.0f, 3.0f, 4.0f);
                }
            }
        }
    }

    // ---- Stretch ratios between markers (marker mode) ----
    if (markerEditMode_ && stretchPairs_.size() >= 2)
    {
        g.setFont(juce::FontOptions(9.0f));
        for (size_t i = 0; i + 1 < stretchPairs_.size(); ++i)
        {
            double srcGap = stretchPairs_[i + 1].src - stretchPairs_[i].src;
            double dstGap = stretchPairs_[i + 1].dst - stretchPairs_[i].dst;
            if (srcGap < 0.001) continue;

            float ratio = static_cast<float>(dstGap / srcGap);

            // Convert absolute src positions to display time (relative to take start)
            float t1 = static_cast<float>(stretchPairs_[i].src - takeOffset_);
            float t2 = static_cast<float>(stretchPairs_[i + 1].src - takeOffset_);
            float x1 = timeToX(t1);
            float x2 = timeToX(t2);

            // Skip if not visible or too narrow for text
            if (x2 < 0 || x1 > w || (x2 - x1) < 28.0f) continue;

            // Color code: green=ok, gold=moderate, red=problem
            juce::Colour col;
            if (ratio >= 0.93f && ratio <= 1.07f)
                col = juce::Colour(0x995cb85c);  // green
            else if (ratio >= 0.7f && ratio <= 1.3f)
                col = juce::Colour(0x99c8a040);  // gold
            else
                col = juce::Colour(0xbbd94848);  // red

            char ratioStr[16];
            snprintf(ratioStr, sizeof(ratioStr), "%.2fx", ratio);

            float midX = (x1 + x2) * 0.5f;
            float textW = 32.0f;
            float textY = waveH * 0.75f;
            g.setColour(col);
            g.drawText(ratioStr, static_cast<int>(midX - textW * 0.5f),
                       static_cast<int>(textY), static_cast<int>(textW), 12,
                       juce::Justification::centred, false);
        }
    }

    // ---- Drag ghost (original position) ----
    float ghostTime = -1;
    if (dragBeatIdx_ >= 0) ghostTime = dragOriginalTime_;
    if (dragMarkerIdx_ >= 0) ghostTime = dragMarkerOriginalTime_;
    if (ghostTime >= 0)
    {
        float gx = timeToX(ghostTime);
        if (gx >= 0 && gx < w)
        {
            g.setColour(WC::goldGhost);
            float dash = 3.0f;
            for (float dy = 6.0f; dy < waveH - 6.0f; dy += dash * 2)
                g.drawVerticalLine(static_cast<int>(gx), dy, std::min(dy + dash, waveH - 6.0f));
        }
    }

    // ---- Ruler ----
    float rulerY = waveH;
    g.setColour(WC::ruleDiv);
    g.drawHorizontalLine(static_cast<int>(rulerY), 0.0f, w);

    // Nice tick interval for current zoom level
    float secPerPx = static_cast<float>(viewDuration_) / w;
    float targetPx = 80.0f;
    float targetSec = secPerPx * targetPx;

    static const float nice[] = {0.1f,0.2f,0.5f, 1,2,5, 10,15,30, 60,120,300};
    float interval = nice[0];
    for (float n : nice) { interval = n; if (n >= targetSec) break; }

    g.setFont(juce::FontOptions(9.0f));
    float tStart = std::floor(static_cast<float>(viewStart_) / interval) * interval;
    float tEnd = static_cast<float>(viewStart_ + viewDuration_);

    for (float t = tStart; t <= tEnd; t += interval)
    {
        float x = timeToX(t);
        if (x < 0 || x >= w - 10) continue;

        g.setColour(WC::ruleTick);
        g.drawVerticalLine(static_cast<int>(x), rulerY + 1.0f, rulerY + 4.0f);

        char label[16];
        if (interval < 1.0f)
            snprintf(label, sizeof(label), "%.1fs", t);
        else
        {
            int sec = static_cast<int>(t);
            int m = sec / 60, s = sec % 60;
            if (m > 0) snprintf(label, sizeof(label), "%d:%02d", m, s);
            else       snprintf(label, sizeof(label), "%ds", s);
        }

        g.setColour(WC::ruleText);
        g.drawText(label, static_cast<int>(x + 2), static_cast<int>(rulerY + 2),
                   42, 12, juce::Justification::centredLeft, false);
    }

    // ---- Scroll position (when zoomed) ----
    if (isZoomed)
    {
        float barY = fullH - kScrollH;
        g.setColour(WC::scrollTrack);
        g.fillRect(0.0f, barY, w, kScrollH);

        float thumbX = static_cast<float>(viewStart_ / duration_) * w;
        float thumbW = std::max(static_cast<float>(viewDuration_ / duration_) * w, 8.0f);
        g.setColour(WC::scrollThumb);
        g.fillRoundedRectangle(thumbX, barY, thumbW, kScrollH, 1.5f);
    }

    // ---- Playhead ----
    if (playheadPos_ >= 0 && playheadPos_ <= duration_)
    {
        float px = timeToX(playheadPos_);
        if (px >= 0 && px <= w)
        {
            g.setColour(WC::playhead);
            g.drawVerticalLine(static_cast<int>(px), 0.0f, fullH);
        }
    }

    // ---- Flash feedback after marker add/delete ----
    if (flashPosition_ >= 0)
    {
        uint32_t elapsed = juce::Time::getMillisecondCounter() - flashTimestamp_;
        if (elapsed < 250)
        {
            float fx = timeToX(flashPosition_);
            if (fx >= 0 && fx < w)
            {
                float progress = static_cast<float>(elapsed) / 250.0f;
                float radius = 4.0f + progress * 16.0f;
                float alpha = 1.0f - progress;
                g.setColour(WC::gold.withAlpha(alpha * 0.6f));
                g.drawEllipse(fx - radius, cx - radius, radius * 2, radius * 2, 1.5f);
            }
        }
        else
        {
            flashPosition_ = -1;
        }
    }

    // ---- Mouse cursor crosshair (thin line showing where click will land) ----
    if (cursorX_ >= 0 && cursorX_ < w && dragBeatIdx_ < 0 && dragMarkerIdx_ < 0)
    {
        g.setColour(juce::Colour(0x40ffffff));
        g.drawVerticalLine(static_cast<int>(cursorX_), 0.0f, waveH);
    }

    // ---- Mode indicator badge (top-right corner) ----
    {
        juce::String modeText = markerEditMode_ ? "MARKERS" : "BEATS";
        auto badgeCol = markerEditMode_ ? WC::gold
                                        : juce::Colour(0xaa909090);
        g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
        juce::GlyphArrangement ga;
        ga.addLineOfText(g.getCurrentFont(), modeText, 0, 0);
        float tw = ga.getBoundingBox(0, -1, false).getWidth();
        float bx = w - tw - 16.0f;
        float by = 5.0f;
        float bw = tw + 12.0f;
        float bh = 16.0f;
        g.setColour(juce::Colour(0xa0000000));
        g.fillRoundedRectangle(bx, by, bw, bh, 4.0f);
        if (markerEditMode_)
        {
            g.setColour(WC::gold.withAlpha(0.3f));
            g.drawRoundedRectangle(bx, by, bw, bh, 4.0f, 1.0f);
        }
        g.setColour(badgeCol);
        g.drawText(modeText, static_cast<int>(bx), static_cast<int>(by),
                   static_cast<int>(bw), static_cast<int>(bh),
                   juce::Justification::centred, false);
    }

    // ---- Gold border in marker edit mode ----
    if (markerEditMode_)
    {
        g.setColour(WC::gold.withAlpha(0.25f));
        g.drawRoundedRectangle(bounds.reduced(0.5f), 4.0f, 1.0f);
    }
}

// ============================================================
//  Mouse interaction
// ============================================================

void WaveformView::mouseDown(const juce::MouseEvent& e)
{
    if (!hasData_ || duration_ <= 0) return;

    mouseDownX_ = static_cast<float>(e.x);
    mouseDownY_ = static_cast<float>(e.y);
    didDrag_ = false;
    potentialDragIdx_ = -1;

    // Ruler area = always seek, never beat interaction
    bool isZoomed = viewDuration_ < duration_ - 0.01;
    float scrollH = isZoomed ? kScrollH : 0.0f;
    float waveH = static_cast<float>(getHeight()) - kRulerH - scrollH;

    if (e.y >= static_cast<int>(waveH))
    {
        double srcTime = xToTime(static_cast<float>(e.x));
        double timeline = srcTimeToTimeline(srcTime);
        if (SetEditCurPos)
            SetEditCurPos(timeline, true, true);
        return;
    }

    // --- Marker edit mode ---
    if (markerEditMode_)
    {
        if (e.mods.isRightButtonDown())
        {
            int idx = findNearestReaperMarker(static_cast<float>(e.x), kBeatHitPx);
            if (idx >= 0 && onMarkerDelete)
            {
                grabKeyboardFocus();
                triggerFlash(reaperMarkers_[idx]);
                onMarkerDelete(reaperMarkers_[idx]);
            }
            return;
        }
        int hit = findNearestReaperMarker(static_cast<float>(e.x), kBeatHitPx);
        if (hit >= 0)
            potentialDragIdx_ = hit;
        return;
    }

    // --- Beat edit mode ---

    // Right-click: delete nearest beat
    if (e.mods.isRightButtonDown())
    {
        int idx = findNearestBeat(static_cast<float>(e.x), kBeatHitPx);
        if (idx >= 0)
        {
            pushUndoState();
            grabKeyboardFocus();
            float bt = beats_[idx];
            beats_.erase(beats_.begin() + idx);

            auto dit = std::find_if(downbeats_.begin(), downbeats_.end(),
                [bt](float db) { return std::abs(db - bt) < 0.025f; });
            if (dit != downbeats_.end())
                downbeats_.erase(dit);

            hoveredBeatIdx_ = -1;
            notifyBeatsEdited();
            repaint();
        }
        return;
    }

    // Left click near beat: record as potential drag (actual drag starts after 3px movement)
    int hit = findNearestBeat(static_cast<float>(e.x), kBeatHitPx);
    if (hit >= 0)
        potentialDragIdx_ = hit;

    // Seek happens in mouseUp if no drag occurred
}

void WaveformView::mouseMove(const juce::MouseEvent& e)
{
    if (!hasData_) return;

    // Track cursor position for crosshair line
    cursorX_ = static_cast<float>(e.x);
    repaint();

    // Ruler area: always pointing hand, no beat hover
    bool isZoomed = viewDuration_ < duration_ - 0.01;
    float scrollH = isZoomed ? kScrollH : 0.0f;
    float waveH = static_cast<float>(getHeight()) - kRulerH - scrollH;

    if (e.y >= static_cast<int>(waveH))
    {
        if (hoveredBeatIdx_ >= 0)
        {
            hoveredBeatIdx_ = -1;
            repaint();
        }
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
        return;
    }

    if (markerEditMode_)
    {
        int newHover = findNearestReaperMarker(static_cast<float>(e.x), kBeatHitPx);
        if (newHover != hoveredMarkerIdx_)
        {
            hoveredMarkerIdx_ = newHover;
            setMouseCursor(newHover >= 0
                ? juce::MouseCursor::LeftRightResizeCursor
                : juce::MouseCursor::PointingHandCursor);
            repaint();
        }
        return;
    }

    int newHover = findNearestBeat(static_cast<float>(e.x), kBeatHitPx);
    if (newHover != hoveredBeatIdx_)
    {
        hoveredBeatIdx_ = newHover;
        setMouseCursor(newHover >= 0
            ? juce::MouseCursor::LeftRightResizeCursor
            : juce::MouseCursor::PointingHandCursor);
        repaint();
    }
}

void WaveformView::mouseDrag(const juce::MouseEvent& e)
{
    // --- Marker edit mode: drag REAPER marker ---
    if (markerEditMode_)
    {
        if (potentialDragIdx_ >= 0 && dragMarkerIdx_ < 0)
        {
            float dx = static_cast<float>(e.x) - mouseDownX_;
            if (std::abs(dx) >= 3.0f)
            {
                didDrag_ = true;
                grabKeyboardFocus();
                dragMarkerIdx_ = potentialDragIdx_;
                dragMarkerOriginalTime_ = reaperMarkers_[potentialDragIdx_];

                // Find corresponding stretchPair for live ratio updates
                dragStretchPairIdx_ = -1;
                double absSrc = static_cast<double>(dragMarkerOriginalTime_) + takeOffset_;
                for (int j = 0; j < static_cast<int>(stretchPairs_.size()); ++j)
                {
                    if (std::abs(stretchPairs_[j].src - absSrc) < 0.005)
                    {
                        dragStretchPairIdx_ = j;
                        dragStretchPairOrigDst_ = stretchPairs_[j].dst;
                        break;
                    }
                }
            }
        }

        if (dragMarkerIdx_ >= 0 && dragMarkerIdx_ < static_cast<int>(reaperMarkers_.size()))
        {
            float newTime = static_cast<float>(
                std::clamp(xToTime(static_cast<float>(e.x)), 0.0, static_cast<double>(duration_)));
            reaperMarkers_[dragMarkerIdx_] = newTime;

            // Update stretchPair so ratio labels reflect the drag in real-time
            if (dragStretchPairIdx_ >= 0 && dragStretchPairIdx_ < static_cast<int>(stretchPairs_.size()))
            {
                double newSrc = static_cast<double>(newTime) + takeOffset_;
                double origSrc = static_cast<double>(dragMarkerOriginalTime_) + takeOffset_;
                stretchPairs_[dragStretchPairIdx_].src = newSrc;
                stretchPairs_[dragStretchPairIdx_].dst = dragStretchPairOrigDst_ + (newSrc - origSrc);
            }

            repaint();
        }
        return;
    }

    // --- Beat edit mode ---

    // Start drag only after 3px movement threshold
    if (potentialDragIdx_ >= 0 && dragBeatIdx_ < 0)
    {
        float dx = static_cast<float>(e.x) - mouseDownX_;
        if (std::abs(dx) >= 3.0f)
        {
            // Commit to drag
            pushUndoState();
            grabKeyboardFocus();
            didDrag_ = true;
            dragBeatIdx_ = potentialDragIdx_;
            dragOriginalTime_ = beats_[potentialDragIdx_];
            dragIsDownbeat_ = isBeatDownbeat(potentialDragIdx_);
            dragDownbeatIdx_ = -1;

            if (dragIsDownbeat_)
            {
                for (int di = 0; di < static_cast<int>(downbeats_.size()); ++di)
                {
                    if (std::abs(downbeats_[di] - beats_[potentialDragIdx_]) < 0.025f)
                    { dragDownbeatIdx_ = di; break; }
                }
            }
        }
    }

    if (dragBeatIdx_ < 0 || dragBeatIdx_ >= static_cast<int>(beats_.size()))
        return;

    float newTime = static_cast<float>(
        std::clamp(xToTime(static_cast<float>(e.x)), 0.0, static_cast<double>(duration_)));

    beats_[dragBeatIdx_] = newTime;

    if (dragIsDownbeat_ && dragDownbeatIdx_ >= 0
        && dragDownbeatIdx_ < static_cast<int>(downbeats_.size()))
        downbeats_[dragDownbeatIdx_] = newTime;

    repaint();
}

void WaveformView::mouseUp(const juce::MouseEvent& e)
{
    // --- Marker edit mode ---
    if (markerEditMode_)
    {
        if (dragMarkerIdx_ >= 0)
        {
            float newTime = reaperMarkers_[dragMarkerIdx_];
            if (onMarkerMove && std::abs(newTime - dragMarkerOriginalTime_) > 0.001f)
            {
                onMarkerMove(dragMarkerOriginalTime_, newTime);
            }
            else
            {
                // Snap back: restore both display position and stretchPair
                reaperMarkers_[dragMarkerIdx_] = dragMarkerOriginalTime_;
                if (dragStretchPairIdx_ >= 0 && dragStretchPairIdx_ < static_cast<int>(stretchPairs_.size()))
                {
                    stretchPairs_[dragStretchPairIdx_].src = static_cast<double>(dragMarkerOriginalTime_) + takeOffset_;
                    stretchPairs_[dragStretchPairIdx_].dst = dragStretchPairOrigDst_;
                }
            }

            dragMarkerIdx_ = -1;
            dragMarkerOriginalTime_ = 0;
            dragStretchPairIdx_ = -1;
            repaint();
        }
        else if (!didDrag_ && !e.mods.isRightButtonDown())
        {
            double srcTime = xToTime(mouseDownX_);
            double timeline = srcTimeToTimeline(srcTime);
            if (SetEditCurPos)
                SetEditCurPos(timeline, true, true);
        }
        potentialDragIdx_ = -1;
        didDrag_ = false;
        return;
    }

    // --- Beat edit mode ---
    if (dragBeatIdx_ >= 0)
    {
        // Finalize drag
        std::sort(beats_.begin(), beats_.end());
        std::sort(downbeats_.begin(), downbeats_.end());

        dragBeatIdx_ = -1;
        dragDownbeatIdx_ = -1;
        dragIsDownbeat_ = false;
        dragOriginalTime_ = 0;

        notifyBeatsEdited();
        repaint();
    }
    else if (!didDrag_ && !e.mods.isRightButtonDown())
    {
        // No drag happened - seek to click position
        double srcTime = xToTime(mouseDownX_);
        double timeline = srcTimeToTimeline(srcTime);
        if (SetEditCurPos)
            SetEditCurPos(timeline, true, true);
    }

    potentialDragIdx_ = -1;
    didDrag_ = false;
}

void WaveformView::mouseDoubleClick(const juce::MouseEvent& e)
{
    if (!hasData_) return;

    // --- Marker edit mode ---
    if (markerEditMode_)
    {
        dragMarkerIdx_ = -1;

        int hit = findNearestReaperMarker(static_cast<float>(e.x), kBeatHitPx);
        if (hit >= 0) return; // double-click on existing marker: no-op

        float newTime = snapToPeak(static_cast<float>(xToTime(static_cast<float>(e.x))));
        if (newTime < 0 || newTime > duration_) return;

        for (float m : reaperMarkers_)
            if (std::abs(m - newTime) < 0.020f) return;

        if (onMarkerAdd)
        {
            grabKeyboardFocus();
            triggerFlash(newTime);
            onMarkerAdd(newTime);
        }
        return;
    }

    // --- Beat edit mode ---

    // Cancel any drag that mouseDown may have started
    dragBeatIdx_ = -1;
    dragDownbeatIdx_ = -1;
    dragIsDownbeat_ = false;

    int hit = findNearestBeat(static_cast<float>(e.x), kBeatHitPx);
    if (hit >= 0)
    {
        // Double-click existing beat: toggle downbeat status
        pushUndoState();
        grabKeyboardFocus();
        float bt = beats_[hit];
        auto dit = std::find_if(downbeats_.begin(), downbeats_.end(),
            [bt](float db) { return std::abs(db - bt) < 0.025f; });

        if (dit != downbeats_.end())
            downbeats_.erase(dit);     // was downbeat - demote
        else
        {
            auto pos = std::lower_bound(downbeats_.begin(), downbeats_.end(), bt);
            downbeats_.insert(pos, bt);  // promote to downbeat
        }

        notifyBeatsEdited();
        repaint();
        return;
    }

    // Double-click empty: add new beat (snapped to nearest peak)
    float newTime = snapToPeak(static_cast<float>(xToTime(static_cast<float>(e.x))));
    if (newTime < 0 || newTime > duration_) return;

    // Skip if too close to existing beat (< 20ms)
    for (float bt : beats_)
        if (std::abs(bt - newTime) < 0.020f) return;

    pushUndoState();
    grabKeyboardFocus();
    auto pos = std::lower_bound(beats_.begin(), beats_.end(), newTime);
    beats_.insert(pos, newTime);

    notifyBeatsEdited();
    repaint();
}

void WaveformView::mouseExit(const juce::MouseEvent&)
{
    hoveredBeatIdx_ = -1;
    hoveredMarkerIdx_ = -1;
    cursorX_ = -1;
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
    repaint();
}

void WaveformView::mouseWheelMove(const juce::MouseEvent& e,
                                    const juce::MouseWheelDetails& wheel)
{
    if (!hasData_ || duration_ <= 0) return;

    bool isZoomed = viewDuration_ < duration_ - 0.01;

    // Horizontal gesture (trackpad) or Shift+wheel = pan
    if (std::abs(wheel.deltaX) > std::abs(wheel.deltaY) * 0.5f
        || e.mods.isShiftDown())
    {
        if (!isZoomed) return;
        double pan = (wheel.deltaX != 0.0f)
            ? -static_cast<double>(wheel.deltaX) * viewDuration_ * 0.4
            : -static_cast<double>(wheel.deltaY) * viewDuration_ * 0.15;

        viewStart_ = std::clamp(viewStart_ + pan,
                                0.0, static_cast<double>(duration_) - viewDuration_);
        followPlayhead_ = false;
        repaint();
        return;
    }

    // Vertical scroll = zoom centered on cursor
    if (wheel.deltaY == 0.0f) return;

    double zoomAt = xToTime(static_cast<float>(e.x));
    double factor = wheel.deltaY > 0 ? 0.8 : 1.25;

    double newDur = viewDuration_ * factor;
    double minView = std::max(kMinViewSec,
        50.0 / static_cast<double>(peaks_.size()) * static_cast<double>(duration_));
    newDur = std::clamp(newDur, minView, static_cast<double>(duration_));

    // Keep zoom center at same pixel position
    double ratio = std::clamp(static_cast<double>(e.x) / getWidth(), 0.0, 1.0);
    viewStart_ = zoomAt - ratio * newDur;
    viewDuration_ = newDur;

    if (viewStart_ < 0) viewStart_ = 0;
    if (viewStart_ + viewDuration_ > duration_)
        viewStart_ = static_cast<double>(duration_) - viewDuration_;

    hoveredBeatIdx_ = -1;
    followPlayhead_ = false;  // user navigating - stop auto-scroll
    repaint();
}

// ============================================================
//  Playhead tracking + beat crossing
// ============================================================

void WaveformView::timerCallback()
{
    if (!hasData_ || !GetPlayState || !GetPlayPosition) return;

    int state = GetPlayState();
    if (state & 1)
    {
        double playPos = GetPlayPosition();
        float srcPos = static_cast<float>((playPos - itemPos_) * playrate_ + takeOffset_);

        if (srcPos != playheadPos_)
        {
            if (!beats_.empty())
            {
                auto it = std::upper_bound(beats_.begin(), beats_.end(), srcPos);
                int cur = static_cast<int>(std::distance(beats_.begin(), it)) - 1;

                if (lastBeatIdx_ == -1)
                {
                    // Playback just started - re-enable follow
                    followPlayhead_ = true;
                    lastBeatIdx_ = cur;
                }
                else if (cur > lastBeatIdx_)
                {
                    bool isDb = false;
                    if (cur >= 0 && cur < static_cast<int>(beats_.size()))
                    {
                        float bt = beats_[cur];
                        for (float db : downbeats_)
                        {
                            if (std::abs(db - bt) < 0.025f) { isDb = true; break; }
                            if (db > bt + 0.025f) break;
                        }
                    }
                    if (onBeatCrossed) onBeatCrossed(isDb);
                    lastBeatIdx_ = cur;
                }
                else if (cur < lastBeatIdx_)
                {
                    lastBeatIdx_ = cur;
                }
            }

            playheadPos_ = srcPos;

            // Auto-scroll to follow playhead when zoomed
            if (followPlayhead_ && viewDuration_ < duration_ - 0.01)
            {
                double ph = static_cast<double>(srcPos);
                if (ph < viewStart_ || ph > viewStart_ + viewDuration_)
                {
                    // Playhead left visible range - scroll so it's at ~15% from left
                    viewStart_ = ph - viewDuration_ * 0.15;
                    viewStart_ = std::clamp(viewStart_, 0.0,
                                            static_cast<double>(duration_) - viewDuration_);
                }
            }

            repaint();
        }
    }
    else
    {
        if (playheadPos_ >= 0)
        {
            playheadPos_ = -1;
            lastBeatIdx_ = -1;
            followPlayhead_ = true;
            repaint();
        }
    }

    // Keep animating flash even when stopped
    if (flashPosition_ >= 0)
        repaint();
}
