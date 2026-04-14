#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "BeatDetector.h"
#include "WaveformView.h"
#include <unordered_map>

// Main UI component for ReaBeat native extension.

class MainComponent : public juce::Component,
                      public juce::Timer,
                      public juce::Button::Listener,
                      public juce::ComboBox::Listener
{
public:
    MainComponent();
    ~MainComponent() override;

    // Callback: toggle dock/undock (set by DockableWindow)
    std::function<void()> onToggleDock;
    std::function<bool()> onIsDocked;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void timerCallback() override;
    void buttonClicked(juce::Button* button) override;
    void comboBoxChanged(juce::ComboBox* combo) override;

private:
    // --- UI Components ---

    // Header
    juce::Label titleLabel;
    juce::Label versionLabel;
    juce::TextButton metronomeBtn{"Met"};
    juce::TextButton supportButton{"Support"};

    // Source info
    juce::Label sourceLabel;
    juce::TextButton detectButton{"Detect Beats"};

    // Waveform with beat markers
    WaveformView waveformView;

    // Progress
    std::unique_ptr<juce::ProgressBar> progressBar;
    double progressValue = 0.0;
    juce::Label progressLabel;

    // Results
    juce::Label bpmLabel;
    juce::TextButton bpmHalfBtn{"/2"};
    juce::TextEditor bpmEditor;
    juce::TextButton bpmDoubleBtn{"x2"};
    juce::Label bpmOriginalLabel;   // "(was X)" after editing
    juce::TextButton setSessionTempoBtn{"Set session"};
    juce::Label timeSigLabel;
    juce::Label beatCountLabel;
    juce::Label confidenceLabel;

    // Action mode (painted as radio circles in paint())
    juce::Label actionLabel;
    juce::ToggleButton matchTempoRadio{"Match Tempo"};
    juce::ToggleButton tempoMapRadio{"Insert Tempo Map"};
    juce::ToggleButton stretchRadio{"Insert Stretch Markers"};

    // Match Tempo options
    juce::Label matchRefLabel;      // "Match to:"
    juce::ComboBox matchRefCombo;   // Reference item dropdown
    juce::Label targetBpmLabel;     // "Target BPM:"
    juce::TextEditor targetBpmEditor;
    juce::ToggleButton alignCheckbox{"Align first downbeat to bar"};

    // Tempo Map options
    juce::ComboBox tempoMapModeCombo;

    // Stretch Markers options
    juce::ComboBox markerModeCombo;
    juce::Label quantizeLabel;
    juce::ComboBox quantizeCombo;
    juce::Label strengthLabel;
    juce::Slider strengthSlider;
    juce::ComboBox stretchQualityCombo;

    // Apply
    juce::TextButton applyButton{"Apply"};

    // Status
    juce::Label statusLabel;
    juce::ToggleButton tooltipCheckbox{"Tooltips"};
    std::unique_ptr<juce::TooltipWindow> tooltipWindow_;

    // --- State ---

    enum ActionMode { kMatchTempo = 0, kTempoMap = 1, kStretchMarkers = 2 };
    ActionMode actionMode_ = kMatchTempo;

    struct ItemState
    {
        void* item = nullptr;
        void* take = nullptr;
        std::string guid;
        std::string name;
        float duration = 0;
        std::string audioPath;
    };
    ItemState currentItem_;

    bool detecting_ = false;
    bool detected_ = false;
    DetectionResult detection_;
    float originalTempo_ = 0;

    // Beat flash indicator
    bool beatFlashing_ = false;
    bool beatFlashDownbeat_ = false;
    uint32_t beatFlashTime_ = 0;
    int bpmRowY_ = 0;  // y position of BPM row, set in resized()

    std::unordered_map<std::string, DetectionResult> cache_;
    int lastStretchMarkerCount_ = -1; // poll for REAPER stretch marker changes
    int markerModeRereadCounter_ = 0; // periodic re-read in marker mode (every ~1s)

    // Reference item info for "Match to:" dropdown
    struct CachedItemInfo {
        std::string name;
        float tempo;
        float confidence;
        double firstDownbeatTimeline;  // where first downbeat plays on timeline
    };
    std::unordered_map<std::string, CachedItemInfo> cacheInfo_;
    std::vector<std::string> matchRefGuids_;
    std::unordered_map<std::string, std::string> matchRefSelection_; // itemGUID -> refGUID
    void rebuildMatchRefCombo();

    class DetectionThread;
    std::unique_ptr<DetectionThread> detectionThread_;

    BeatDetector beatDetector_;
    bool modelLoaded_ = false;

    // --- Methods ---

    void updateSelectedItem();
    void startDetection();
    void onDetectionComplete(const DetectionResult& result);
    void applyAction();
    void updateUI();
    void showResults(bool show);
    void setActionMode(ActionMode mode);
    void setStatus(const juce::String& msg, juce::Colour colour);
    void loadOrDownloadModel();

    // Custom radio button painting
    void paintRadioButton(juce::Graphics& g, juce::ToggleButton& btn);

    // Colors
    struct Colors
    {
        static inline juce::Colour bg{0xff1e1e1e};
        static inline juce::Colour accent{0xffc8a040};
        static inline juce::Colour text{0xffe8e8e8};
        static inline juce::Colour textDim{0xff808080};
        static inline juce::Colour success{0xff5cb85c};
        static inline juce::Colour error{0xffd94848};
        static inline juce::Colour warning{0xffe8a838};
        static inline juce::Colour inputBg{0xff2a2a2a};
        static inline juce::Colour buttonBg{0xff3a3a3a};
        static inline juce::Colour confHigh{0xff5cb85c};
        static inline juce::Colour confMed{0xffc8a040};
        static inline juce::Colour confLow{0xffd94848};
    };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
