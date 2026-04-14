#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>
#include <functional>

// Interactive waveform editor with beat markers, zoom/scroll, and beat editing.
// Professional musicians can add, delete, drag beats, and toggle downbeat status.
//
// Navigation:
//   Scroll wheel        = zoom (centered on cursor)
//   Shift + scroll      = horizontal pan
//   Trackpad swipe      = horizontal pan
//   Click               = seek to position in REAPER
//
// Beat editing:
//   Hover near beat     = highlight + resize cursor
//   Drag beat           = adjust position (ghost shows original)
//   Double-click empty  = add new beat
//   Double-click beat   = toggle downbeat status
//   Right-click beat    = delete beat

class WaveformView : public juce::Component,
                     public juce::Timer
{
public:
    WaveformView();

    void setData(const std::vector<float>& peaks,  // RMS envelope (100/sec, 0-1)
                 const std::vector<float>& beats,
                 const std::vector<float>& downbeats,
                 float duration);

    void setItemInfo(double itemPos, double takeOffset, double playrate);
    // Set REAPER stretch markers for display + seek correction.
    // srcPositions: source time (for display), pairs: src/dst (for seek interpolation)
    void setReaperMarkers(const std::vector<float>& srcPositions,
                          const std::vector<std::pair<double,double>>& pairs);
    void clear();
    void zoomToFit();

    // Callback: playhead crossed a beat during playback (param: isDownbeat)
    std::function<void(bool)> onBeatCrossed;

    // Callback: user edited beats - receives updated beats and downbeats (beat mode)
    std::function<void(const std::vector<float>&, const std::vector<float>&)> onBeatsEdited;

    // Callbacks: direct REAPER stretch marker editing (marker mode)
    std::function<void(float srcTime)> onMarkerAdd;
    std::function<void(float oldSrc, float newSrc)> onMarkerMove;
    std::function<void(float srcTime)> onMarkerDelete;

    // Callback: user pressed Enter in waveform (triggers Apply)
    std::function<void()> onApplyRequested;

    // Navigate to next gap >1.35x median (N key). Returns false if no gaps.
    bool scrollToNextGap();

    // Flash at position (visual feedback after marker add/delete)
    void triggerFlash(float position);

    // BPM for grid overlay in marker mode (set after detection)
    void setGridBpm(float bpm, float firstBeat);

    // Snap time to nearest RMS peak within +/-windowSec
    float snapToPeak(float time, float windowSec = 0.030f) const;

    // Marker edit mode: after Apply, edits go directly to REAPER markers
    void setMarkerEditMode(bool enabled);
    bool isMarkerEditMode() const { return markerEditMode_; }
    bool isDraggingMarker() const { return dragMarkerIdx_ >= 0; }

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override;
    void mouseExit(const juce::MouseEvent& e) override;
    bool keyPressed(const juce::KeyPress& key) override;
    void timerCallback() override;

private:
    // Audio data
    std::vector<float> peaks_;
    std::vector<float> beats_;       // mutable: user can edit
    std::vector<float> downbeats_;   // mutable: user can edit
    // REAPER stretch markers: source positions (for display) + pairs (for seek)
    std::vector<float> reaperMarkers_;
    struct StretchPair { double src; double dst; };
    std::vector<StretchPair> stretchPairs_; // sorted by src
    float duration_ = 0;
    bool hasData_ = false;

    // Item timeline (REAPER seek + playhead)
    double itemPos_ = 0;
    double takeOffset_ = 0;
    double playrate_ = 1.0;
    float playheadPos_ = -1;
    int lastBeatIdx_ = -1;

    // Zoom and scroll
    double viewStart_ = 0.0;
    double viewDuration_ = 0.0;
    bool followPlayhead_ = true;  // auto-scroll to keep playhead visible

    // Beat interaction (beat mode)
    int hoveredBeatIdx_ = -1;
    int dragBeatIdx_ = -1;
    int dragDownbeatIdx_ = -1;
    bool dragIsDownbeat_ = false;
    float dragOriginalTime_ = 0;
    bool beatsEdited_ = false;

    // Marker interaction (marker mode)
    bool markerEditMode_ = false;
    int hoveredMarkerIdx_ = -1;
    int dragMarkerIdx_ = -1;
    float dragMarkerOriginalTime_ = 0;
    int dragStretchPairIdx_ = -1;     // corresponding stretchPairs_ entry during drag
    double dragStretchPairOrigDst_ = 0;
    float cursorX_ = -1;  // mouse cursor X for crosshair line

    // Flash feedback after marker add/delete
    float flashPosition_ = -1;
    uint32_t flashTimestamp_ = 0;

    // Grid overlay (marker mode)
    float gridBpm_ = 0;
    float gridFirstBeat_ = 0;

    // Click vs drag disambiguation (3px threshold)
    int potentialDragIdx_ = -1;
    float mouseDownX_ = 0;
    float mouseDownY_ = 0;
    bool didDrag_ = false;

    // Undo/redo for beat edits
    struct BeatSnapshot { std::vector<float> beats, downbeats; };
    std::vector<BeatSnapshot> undoStack_;
    std::vector<BeatSnapshot> redoStack_;
    static constexpr int kMaxUndo = 50;
    void pushUndoState();

    // Helpers
    float timeToX(double time) const;
    double xToTime(float x) const;
    int findNearestBeat(float x, float maxDistPx = 8.0f) const;
    bool isBeatDownbeat(int beatIdx) const;
    void notifyBeatsEdited();
    double srcTimeToTimeline(double srcTime) const; // accounts for stretch markers
    int findNearestReaperMarker(float x, float maxDistPx = 8.0f) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WaveformView)
};
