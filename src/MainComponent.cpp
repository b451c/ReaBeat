#include "MainComponent.h"
#include "ReaperActions.h"
#include "ModelManager.h"
#include "FilenameParser.h"
#include "BinaryData.h"

#include "reaper_plugin.h"
#include "reaper_plugin_functions.h"

// --- Custom LookAndFeel for radio buttons (circles instead of checkboxes) ---

class RadioLookAndFeel : public juce::LookAndFeel_V4
{
public:
    void drawToggleButton(juce::Graphics& g, juce::ToggleButton& btn,
                          bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override
    {
        auto fontSize = juce::jmin(15.0f, static_cast<float>(btn.getHeight()) * 0.75f);
        auto tickWidth = fontSize * 1.1f;
        auto r = juce::Rectangle<float>(4.0f, (btn.getHeight() - tickWidth) * 0.5f, tickWidth, tickWidth);

        if (btn.getRadioGroupId() > 0)
        {
            // Draw as radio button (circle)
            g.setColour(juce::Colour(0xff3a3a3a));
            g.fillEllipse(r);
            g.setColour(juce::Colour(0xff555555));
            g.drawEllipse(r.reduced(0.5f), 1.0f);

            if (btn.getToggleState())
            {
                auto inner = r.reduced(tickWidth * 0.25f);
                g.setColour(juce::Colour(0xffc8a040));
                g.fillEllipse(inner);
            }
        }
        else
        {
            // Draw as checkbox (square with tick)
            g.setColour(juce::Colour(0xff3a3a3a));
            g.fillRoundedRectangle(r, 2.0f);
            g.setColour(juce::Colour(0xff555555));
            g.drawRoundedRectangle(r.reduced(0.5f), 2.0f, 1.0f);

            if (btn.getToggleState())
            {
                g.setColour(juce::Colour(0xffc8a040));
                auto tick = r.reduced(3.0f);
                g.drawLine(tick.getX(), tick.getCentreY(),
                           tick.getCentreX(), tick.getBottom(), 2.0f);
                g.drawLine(tick.getCentreX(), tick.getBottom(),
                           tick.getRight(), tick.getY(), 2.0f);
            }
        }

        g.setColour(btn.findColour(juce::ToggleButton::textColourId));
        g.setFont(fontSize);
        g.drawFittedText(btn.getButtonText(),
                         btn.getLocalBounds().withTrimmedLeft(static_cast<int>(tickWidth + 8.0f)),
                         juce::Justification::centredLeft, 1);
    }
};

static RadioLookAndFeel radioLookAndFeel;

// --- Background detection thread ---

class MainComponent::DetectionThread : public juce::Thread
{
public:
    DetectionThread(MainComponent& owner, const std::string& filePath,
                    const std::string& itemGuid, const std::string& itemName)
        : juce::Thread("ReaBeat Detection"),
          safeOwner_(&owner),
          filePath_(filePath),
          itemGuid_(itemGuid),
          itemName_(itemName),
          detector_(owner.beatDetector_) {}

    void run() override
    {
        // shouldCancel makes stopThread() cooperative - without it the
        // destructor's timeout would killThread() mid Ort::Run
        auto result = detector_.detectFile(filePath_,
            [this](const std::string& msg, float frac)
            {
                auto safe = safeOwner_;
                juce::MessageManager::callAsync([safe, msg, frac]()
                {
                    if (auto* owner = safe.getComponent())
                    {
                        owner->progressValue = frac;
                        owner->progressLabel.setText(msg, juce::dontSendNotification);
                    }
                });
            },
            [this]() { return threadShouldExit(); });

        auto safe = safeOwner_;
        juce::MessageManager::callAsync(
            [safe, result = std::move(result), guid = itemGuid_, name = itemName_]()
        {
            if (auto* owner = safe.getComponent())
                owner->onDetectionComplete(result, guid, name);
        });
    }

private:
    juce::Component::SafePointer<MainComponent> safeOwner_;
    std::string filePath_;
    std::string itemGuid_;
    std::string itemName_;
    BeatDetector& detector_;
};

class MainComponent::ModelDownloadThread : public juce::Thread
{
public:
    ModelDownloadThread(MainComponent& owner)
        : juce::Thread("ReaBeat Model Download"),
          safeOwner_(&owner) {}

    void run() override
    {
        bool ok = ModelManager::downloadModel([this](float progress)
        {
            auto safe = safeOwner_;
            juce::MessageManager::callAsync([safe, progress]()
            {
                if (auto* owner = safe.getComponent())
                {
                    char msg[64];
                    snprintf(msg, sizeof(msg), "Downloading model: %d%%",
                             static_cast<int>(progress * 100));
                    owner->progressValue = progress;
                    owner->progressLabel.setText(msg, juce::dontSendNotification);
                    owner->setStatus(msg, Colors::warning);
                }
            });
        },
        [this]() { return threadShouldExit(); });

        auto safe = safeOwner_;
        juce::MessageManager::callAsync([safe, ok]()
        {
            if (auto* owner = safe.getComponent())
                owner->onModelDownloadComplete(ok);
        });
    }

private:
    juce::Component::SafePointer<MainComponent> safeOwner_;
};

// --- Constructor ---

MainComponent::MainComponent()
{
    // Header
    titleLabel.setText("ReaBeat", juce::dontSendNotification);
    titleLabel.setFont(juce::FontOptions(18.0f, juce::Font::bold));
    titleLabel.setColour(juce::Label::textColourId, Colors::accent);
    titleLabel.setInterceptsMouseClicks(true, false);
    titleLabel.addMouseListener(this, false);
    addAndMakeVisible(titleLabel);

    versionLabel.setText("v" REABEAT_VERSION, juce::dontSendNotification);
    versionLabel.setFont(juce::FontOptions(11.0f));
    versionLabel.setColour(juce::Label::textColourId, Colors::textDim);
    addAndMakeVisible(versionLabel);

    metronomeBtn.setColour(juce::TextButton::buttonColourId, Colors::buttonBg);
    metronomeBtn.setColour(juce::TextButton::textColourOnId, Colors::textDim);
    metronomeBtn.setTooltip("Toggle REAPER metronome");
    metronomeBtn.addListener(this);
    addChildComponent(metronomeBtn);

    supportButton.setColour(juce::TextButton::buttonColourId, Colors::buttonBg);
    supportButton.setColour(juce::TextButton::textColourOnId, Colors::textDim);
    supportButton.addListener(this);
    addAndMakeVisible(supportButton);

    // Source
    sourceLabel.setText("Select an audio item in REAPER", juce::dontSendNotification);
    sourceLabel.setFont(juce::FontOptions(12.0f));
    sourceLabel.setColour(juce::Label::textColourId, Colors::textDim);
    addAndMakeVisible(sourceLabel);

    detectButton.setColour(juce::TextButton::buttonColourId, Colors::accent);
    detectButton.setColour(juce::TextButton::textColourOnId, juce::Colour(0xff1e1e1e));
    detectButton.addListener(this);
    detectButton.setEnabled(false);
    detectButton.setTooltip("Run neural beat detection on the selected audio item");
    addAndMakeVisible(detectButton);

    // Waveform
    addChildComponent(waveformView);
    waveformView.onBeatCrossed = [this](bool isDownbeat)
    {
        beatFlashing_ = true;
        beatFlashDownbeat_ = isDownbeat;
        beatFlashTime_ = juce::Time::getMillisecondCounter();
        repaint();
    };

    waveformView.onBeatsEdited = [this](const std::vector<float>& beats,
                                        const std::vector<float>& downbeats)
    {
        detection_.beats = beats;
        detection_.downbeats = downbeats;

        // Update beat/bar counts
        int bars = downbeats.empty() ? 0 : static_cast<int>(downbeats.size());
        char bc[64];
        snprintf(bc, sizeof(bc), "%zu beats | %d bars", beats.size(), bars);
        beatCountLabel.setText(bc, juce::dontSendNotification);

        char msg[64];
        snprintf(msg, sizeof(msg), "%zu beats (edited)", beats.size());
        setStatus(msg, Colors::warning);
    };

    waveformView.onApplyRequested = [this]() { applyAction(); };

    // Marker edit mode callbacks: direct REAPER stretch marker manipulation.
    // Detection beat times ARE source positions (detection reads the source
    // file from 0), so srcTime maps to REAPER srcpos with no takeOffset term.
    waveformView.onMarkerAdd = [this](float srcTime)
    {
        if (!currentItem_.take || !currentItem_.item) return;
        auto* take = static_cast<MediaItem_Take*>(currentItem_.take);
        auto* item = static_cast<MediaItem*>(currentItem_.item);
        double src = static_cast<double>(srcTime);
        double dst = ReaperActions::interpolateDst(take, src);
        ReaperActions::addOneStretchMarker(take, item, src, dst);
        lastStretchMarkerCount_ = -1;
        setStatus("Stretch marker added", Colors::success);
    };

    waveformView.onMarkerMove = [this](float oldSrc, float newSrc)
    {
        if (!currentItem_.take || !currentItem_.item) return;
        auto* take = static_cast<MediaItem_Take*>(currentItem_.take);
        auto* item = static_cast<MediaItem*>(currentItem_.item);
        ReaperActions::moveOneStretchMarker(take, item,
            static_cast<double>(oldSrc), static_cast<double>(newSrc));
        lastStretchMarkerCount_ = -1;
        setStatus("Stretch marker moved", Colors::success);
    };

    waveformView.onMarkerDelete = [this](float srcTime)
    {
        if (!currentItem_.take || !currentItem_.item) return;
        auto* take = static_cast<MediaItem_Take*>(currentItem_.take);
        auto* item = static_cast<MediaItem*>(currentItem_.item);
        ReaperActions::deleteOneStretchMarker(take, item,
            static_cast<double>(srcTime));
        lastStretchMarkerCount_ = -1;
        setStatus("Stretch marker deleted", Colors::success);
    };

    // Progress
    progressBar = std::make_unique<juce::ProgressBar>(progressValue);
    progressBar->setColour(juce::ProgressBar::foregroundColourId, Colors::accent);
    addChildComponent(progressBar.get());

    progressLabel.setFont(juce::FontOptions(11.0f));
    progressLabel.setColour(juce::Label::textColourId, Colors::textDim);
    progressLabel.setJustificationType(juce::Justification::centred);
    addChildComponent(progressLabel);

    // Results
    bpmLabel.setText("BPM:", juce::dontSendNotification);
    bpmLabel.setFont(juce::FontOptions(13.0f));
    bpmLabel.setColour(juce::Label::textColourId, Colors::text);
    addChildComponent(bpmLabel);

    auto setupOctaveBtn = [this](juce::TextButton& btn, const juce::String& tooltip) {
        btn.setColour(juce::TextButton::buttonColourId, Colors::inputBg);
        btn.setColour(juce::TextButton::textColourOnId, Colors::textDim);
        btn.setTooltip(tooltip);
        btn.addListener(this);
        addChildComponent(btn);
    };
    setupOctaveBtn(bpmHalfBtn, "Half tempo (octave down)");
    setupOctaveBtn(bpmDoubleBtn, "Double tempo (octave up)");

    bpmEditor.setFont(juce::FontOptions(16.0f, juce::Font::bold));
    bpmEditor.setColour(juce::TextEditor::backgroundColourId, Colors::inputBg);
    bpmEditor.setColour(juce::TextEditor::textColourId, Colors::accent);
    bpmEditor.setJustification(juce::Justification::centred);
    bpmEditor.setInputRestrictions(8, "0123456789.");
    bpmEditor.onTextChange = [this]()
    {
        float edited = bpmEditor.getText().getFloatValue();
        bool wasVisible = bpmOriginalLabel.isVisible();
        bool nowVisible = (edited > 0 && std::abs(edited - originalTempo_) > 0.05f);
        if (nowVisible)
        {
            char was[32];
            snprintf(was, sizeof(was), "(was %.1f)", originalTempo_);
            bpmOriginalLabel.setText(was, juce::dontSendNotification);
        }
        bpmOriginalLabel.setVisible(nowVisible);
        // The layout reserves an extra row for the label - relayout on
        // visibility change or the label overlaps the Action row
        if (wasVisible != nowVisible)
            resized();
    };
    addChildComponent(bpmEditor);

    bpmOriginalLabel.setFont(juce::FontOptions(10.0f));
    bpmOriginalLabel.setColour(juce::Label::textColourId, Colors::textDim);
    addChildComponent(bpmOriginalLabel);

    setSessionTempoBtn.setColour(juce::TextButton::buttonColourId, Colors::buttonBg);
    setSessionTempoBtn.setColour(juce::TextButton::textColourOnId, Colors::accent);
    setSessionTempoBtn.setTooltip("Set REAPER session tempo to detected BPM");
    setSessionTempoBtn.addListener(this);
    addChildComponent(setSessionTempoBtn);

    // Time signature: shows the detected meter, allows manual override.
    // Compound meters (6/8, 9/8, 12/8) are commonly detected as 4/4 - the
    // override recomputes downbeats from the chosen meter.
    timeSigCombo.addItem("Auto", 1);
    timeSigCombo.addItem("2/4", 2);
    timeSigCombo.addItem("3/4", 3);
    timeSigCombo.addItem("4/4", 4);
    timeSigCombo.addItem("6/8", 5);
    timeSigCombo.addItem("9/8", 6);
    timeSigCombo.addItem("12/8", 7);
    timeSigCombo.setSelectedId(1, juce::dontSendNotification);
    timeSigCombo.setColour(juce::ComboBox::backgroundColourId, Colors::inputBg);
    timeSigCombo.setColour(juce::ComboBox::textColourId, Colors::text);
    timeSigCombo.setTooltip("Time signature. Auto = neural detection; pick a meter to recompute bars manually (6/8, 9/8, 12/8 are often misdetected as 4/4)");
    timeSigCombo.onChange = [this]() { applyTimeSigSelection(timeSigCombo.getSelectedId()); };
    addChildComponent(timeSigCombo);

    beatCountLabel.setFont(juce::FontOptions(11.0f));
    beatCountLabel.setColour(juce::Label::textColourId, Colors::textDim);
    addChildComponent(beatCountLabel);

    confidenceLabel.setFont(juce::FontOptions(11.0f));
    addChildComponent(confidenceLabel);

    // Action mode - use custom LookAndFeel for radio circles
    actionLabel.setText("Action", juce::dontSendNotification);
    actionLabel.setFont(juce::FontOptions(13.0f, juce::Font::bold));
    actionLabel.setColour(juce::Label::textColourId, Colors::text);
    addChildComponent(actionLabel);

    matchTempoRadio.setRadioGroupId(1);
    tempoMapRadio.setRadioGroupId(1);
    stretchRadio.setRadioGroupId(1);
    matchTempoRadio.setToggleState(true, juce::dontSendNotification);

    auto setupRadio = [this](juce::ToggleButton& btn, const juce::String& tooltip) {
        btn.setLookAndFeel(&radioLookAndFeel);
        btn.setColour(juce::ToggleButton::textColourId, Colors::text);
        btn.setColour(juce::ToggleButton::tickColourId, Colors::accent);
        btn.setTooltip(tooltip);
        btn.onClick = [this]() {
            if (matchTempoRadio.getToggleState()) setActionMode(kMatchTempo);
            else if (tempoMapRadio.getToggleState()) setActionMode(kTempoMap);
            else if (stretchRadio.getToggleState()) setActionMode(kStretchMarkers);
        };
        addChildComponent(btn);
    };
    setupRadio(matchTempoRadio, "Adjust playrate to target BPM (pitch preserved via elastique)");
    setupRadio(tempoMapRadio, "Sync REAPER's grid to audio without modifying the item");
    setupRadio(stretchRadio, "Place stretch markers at detected beat positions");

    // Match Tempo options
    matchRefLabel.setText("Match to:", juce::dontSendNotification);
    matchRefLabel.setFont(juce::FontOptions(11.0f));
    matchRefLabel.setColour(juce::Label::textColourId, Colors::textDim);
    addChildComponent(matchRefLabel);

    matchRefCombo.setColour(juce::ComboBox::backgroundColourId, Colors::inputBg);
    matchRefCombo.setColour(juce::ComboBox::textColourId, Colors::text);
    matchRefCombo.setTooltip("Select reference track for tempo matching");
    matchRefCombo.onChange = [this]()
    {
        int id = matchRefCombo.getSelectedId();

        // Save selection immediately for this item
        if (!currentItem_.guid.empty())
        {
            if (id >= 2)
            {
                int idx = id - 2;
                if (idx >= 0 && idx < static_cast<int>(matchRefGuids_.size()))
                    matchRefSelection_[currentItem_.guid] = matchRefGuids_[idx];
            }
            else
                matchRefSelection_[currentItem_.guid] = "";
        }

        // Update target BPM
        if (id == 1)
        {
            float projBpm = ReaperActions::getProjectBpm();
            targetBpmEditor.setText(juce::String(projBpm, 1), false);
        }
        else if (id >= 2)
        {
            int idx = id - 2;
            if (idx >= 0 && idx < static_cast<int>(matchRefGuids_.size()))
            {
                auto it = cacheInfo_.find(matchRefGuids_[idx]);
                if (it != cacheInfo_.end())
                    targetBpmEditor.setText(juce::String(it->second.tempo, 1), false);
            }
        }
    };
    addChildComponent(matchRefCombo);

    targetBpmLabel.setText("Target BPM:", juce::dontSendNotification);
    targetBpmLabel.setFont(juce::FontOptions(11.0f));
    targetBpmLabel.setColour(juce::Label::textColourId, Colors::textDim);
    addChildComponent(targetBpmLabel);

    targetBpmEditor.setFont(juce::FontOptions(13.0f));
    targetBpmEditor.setColour(juce::TextEditor::backgroundColourId, Colors::inputBg);
    targetBpmEditor.setColour(juce::TextEditor::textColourId, Colors::text);
    targetBpmEditor.setInputRestrictions(8, "0123456789.");
    addChildComponent(targetBpmEditor);

    alignCheckbox.setToggleState(true, juce::dontSendNotification);
    alignCheckbox.setLookAndFeel(&radioLookAndFeel);
    alignCheckbox.setColour(juce::ToggleButton::textColourId, Colors::text);
    alignCheckbox.setColour(juce::ToggleButton::tickColourId, Colors::accent);
    addChildComponent(alignCheckbox);

    // Tempo Map mode
    tempoMapModeCombo.addItem("Constant", 1);
    tempoMapModeCombo.addItem("Variable - bars", 2);
    tempoMapModeCombo.addItem("Variable - beats", 3);
    tempoMapModeCombo.setSelectedId(1);
    tempoMapModeCombo.setColour(juce::ComboBox::backgroundColourId, Colors::inputBg);
    tempoMapModeCombo.setColour(juce::ComboBox::textColourId, Colors::text);
    addChildComponent(tempoMapModeCombo);

    // Stretch Markers options
    markerModeCombo.addItem("Every beat", 1);
    markerModeCombo.addItem("Downbeats only", 2);
    markerModeCombo.setSelectedId(1);
    markerModeCombo.setColour(juce::ComboBox::backgroundColourId, Colors::inputBg);
    markerModeCombo.setColour(juce::ComboBox::textColourId, Colors::text);
    addChildComponent(markerModeCombo);

    quantizeLabel.setText("Quantize:", juce::dontSendNotification);
    quantizeLabel.setFont(juce::FontOptions(11.0f));
    quantizeLabel.setColour(juce::Label::textColourId, Colors::textDim);
    addChildComponent(quantizeLabel);

    quantizeCombo.addItem("Off", 1);
    quantizeCombo.addItem("Straight", 2);
    quantizeCombo.addItem("Bars", 3);
    quantizeCombo.addItem("Project grid", 4);
    quantizeCombo.setSelectedId(2);
    quantizeCombo.setColour(juce::ComboBox::backgroundColourId, Colors::inputBg);
    quantizeCombo.setColour(juce::ComboBox::textColourId, Colors::text);
    quantizeCombo.setTooltip("Off = no stretch | Straight = mathematical grid | Bars = follow downbeats | Project grid = align to REAPER grid (multi-track sync)");
    quantizeCombo.onChange = [this]()
    {
        // Visibility derives from the combo state, not from the slider's
        // pre-update visibility (which lags one change behind)
        bool showStrength = quantizeCombo.getSelectedId() > 1 && quantizeCombo.isVisible();
        strengthLabel.setVisible(showStrength);
        strengthSlider.setVisible(showStrength);
        resized();
    };
    addChildComponent(quantizeCombo);

    strengthLabel.setText("Strength:", juce::dontSendNotification);
    strengthLabel.setFont(juce::FontOptions(11.0f));
    strengthLabel.setColour(juce::Label::textColourId, Colors::textDim);
    addChildComponent(strengthLabel);

    strengthSlider.setRange(0, 100, 1);
    strengthSlider.setValue(100, juce::dontSendNotification);
    strengthSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 36, 18);
    strengthSlider.setColour(juce::Slider::trackColourId, juce::Colour(0xffc8a040));
    strengthSlider.setColour(juce::Slider::thumbColourId, juce::Colour(0xffc8a040));
    strengthSlider.setColour(juce::Slider::backgroundColourId, juce::Colour(0xff2a2a2a));
    strengthSlider.setColour(juce::Slider::textBoxTextColourId, Colors::text);
    strengthSlider.setColour(juce::Slider::textBoxBackgroundColourId, Colors::inputBg);
    strengthSlider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    strengthSlider.setTextValueSuffix("%");
    strengthSlider.setTooltip("Quantize strength: 100% = full grid snap, 50% = halfway, 0% = no change");
    addChildComponent(strengthSlider);

    stretchQualityCombo.addItem("Balanced", 1);
    stretchQualityCombo.addItem("Transient", 2);
    stretchQualityCombo.addItem("Tonal", 3);
    stretchQualityCombo.setSelectedId(1);
    stretchQualityCombo.setColour(juce::ComboBox::backgroundColourId, Colors::inputBg);
    stretchQualityCombo.setColour(juce::ComboBox::textColourId, Colors::text);
    addChildComponent(stretchQualityCombo);

    // Apply
    applyButton.setColour(juce::TextButton::buttonColourId, Colors::accent);
    applyButton.setColour(juce::TextButton::textColourOnId, juce::Colour(0xff1e1e1e));
    applyButton.setTooltip("Apply selected action (Enter)");
    applyButton.addListener(this);
    addChildComponent(applyButton);

    // Status
    statusLabel.setFont(juce::FontOptions(11.0f));
    statusLabel.setColour(juce::Label::textColourId, Colors::textDim);
    statusLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(statusLabel);

    // Tooltips
    tooltipWindow_ = std::make_unique<juce::TooltipWindow>(this, 600);
    tooltipCheckbox.setToggleState(true, juce::dontSendNotification);
    tooltipCheckbox.setLookAndFeel(&radioLookAndFeel);
    tooltipCheckbox.setColour(juce::ToggleButton::textColourId, Colors::textDim);
    tooltipCheckbox.setColour(juce::ToggleButton::tickColourId, Colors::textDim);
    tooltipCheckbox.onClick = [this]()
    {
        if (tooltipCheckbox.getToggleState())
            tooltipWindow_ = std::make_unique<juce::TooltipWindow>(this, 600);
        else
            tooltipWindow_.reset();
    };
    addAndMakeVisible(tooltipCheckbox);

    setSize(460, 660);

    // Restore persisted UI scale (flark / 4K-display request)
    if (GetExtState)
    {
        const char* s = GetExtState("ReaBeat", "uiscale");
        if (s && s[0])
        {
            float f = static_cast<float>(atof(s));
            if (f >= 1.0f && f <= 2.0f && f != 1.0f)
            {
                uiScale_ = f;
                setTransform(juce::AffineTransform::scale(uiScale_));
            }
        }
    }

    loadOrDownloadModel();
    startTimer(200);
}

MainComponent::~MainComponent()
{
    matchTempoRadio.setLookAndFeel(nullptr);
    tempoMapRadio.setLookAndFeel(nullptr);
    stretchRadio.setLookAndFeel(nullptr);
    alignCheckbox.setLookAndFeel(nullptr);
    tooltipCheckbox.setLookAndFeel(nullptr);
    tooltipWindow_.reset();

    stopTimer();
    if (detectionThread_)
        detectionThread_->stopThread(5000);
    if (modelDownloadThread_)
        modelDownloadThread_->stopThread(10000);
}

// --- Paint ---

void MainComponent::paint(juce::Graphics& g)
{
    g.fillAll(Colors::bg);

    auto area = getLocalBounds().reduced(16, 0);
    g.setColour(juce::Colour(0xff333333));

    if (detected_)
    {
        g.drawHorizontalLine(62, static_cast<float>(area.getX()), static_cast<float>(area.getRight()));

        // Beat indicator dot - left of "BPM:" label
        float dotX = static_cast<float>(area.getX() - 12);
        float dotY = static_cast<float>(bpmRowY_ + 8);

        if (beatFlashing_)
        {
            float sz = beatFlashDownbeat_ ? 9.0f : 7.0f;
            float offset = (9.0f - sz) * 0.5f;
            g.setColour(Colors::accent);
            g.fillEllipse(dotX + offset, dotY + offset, sz, sz);
        }
        else
        {
            g.setColour(Colors::accent.withAlpha(0.15f));
            g.fillEllipse(dotX + 1.0f, dotY + 1.0f, 7.0f, 7.0f);
        }
    }
    else
    {
        // Splash:  [icon] ReaBeat
        //          ─────────────────────────
        //          Neural beat detection for REAPER
        int splashTop = 120;
        int splashH = getHeight() - splashTop - 30;
        if (splashH < 100) return;

        static auto icon = juce::ImageFileFormat::loadFrom(
            BinaryData::icon_png, BinaryData::icon_pngSize);

        // Title
        juce::String title("ReaBeat");
        juce::String tagline("Neural beat detection for REAPER");
        float titleFontSize = 44.0f;
        auto titleFont = juce::Font(juce::FontOptions(titleFontSize, juce::Font::bold));
        juce::GlyphArrangement titleGa;
        titleGa.addLineOfText(titleFont, title, 0, 0);
        float titleW = titleGa.getBoundingBox(0, -1, false).getWidth();

        // Icon inline with title: visual height = title cap height
        // SVG content is ~62% of bounding box, so draw bigger to compensate
        int iconVis = static_cast<int>(titleFontSize);
        int iconDraw = static_cast<int>(static_cast<float>(iconVis) / 0.62f);
        int iconPad = (iconDraw - iconVis) / 2;
        int iconGap = 8;

        // Block width = visible icon + gap + title
        float blockW = static_cast<float>(iconVis + iconGap) + titleW;
        float blockX = (static_cast<float>(getWidth()) - blockW) * 0.5f;

        // Compute tagline font size (needed for centering)
        float tagFontSize = 11.0f;
        for (int iter = 0; iter < 5; ++iter)
        {
            auto tagFont = juce::Font(juce::FontOptions(tagFontSize));
            juce::GlyphArrangement tagGa;
            tagGa.addLineOfText(tagFont, tagline, 0, 0);
            float measuredW = tagGa.getBoundingBox(0, -1, false).getWidth();
            tagFontSize *= (blockW / measuredW);
            tagFontSize = std::min(tagFontSize, 16.0f);
        }
        tagFontSize *= 0.95f;

        // Center splash block vertically: title(44) + gap(8) + line(1) + gap(6) + tagline
        int splashBlockH = static_cast<int>(titleFontSize) + 14 + static_cast<int>(tagFontSize) + 6;
        int cy = splashTop + (splashH - splashBlockH) * 2 / 5;  // slightly above center

        // Icon (drawn bigger, offset to compensate SVG padding)
        if (icon.isValid())
        {
            g.setOpacity(0.85f);
            g.drawImage(icon,
                static_cast<int>(blockX) - iconPad, cy - iconPad,
                iconDraw, iconDraw,
                0, 0, icon.getWidth(), icon.getHeight());
            g.setOpacity(1.0f);
        }

        // "ReaBeat" next to icon
        float tx = blockX + static_cast<float>(iconVis + iconGap);
        g.setColour(Colors::accent.withAlpha(0.8f));
        g.setFont(titleFont);
        g.drawText(title, static_cast<int>(tx), cy,
                   static_cast<int>(titleW + 4), static_cast<int>(titleFontSize),
                   juce::Justification::centredLeft);

        // Separator line - full block width
        float lineY = static_cast<float>(cy) + titleFontSize + 8;
        g.setColour(Colors::textDim.withAlpha(0.25f));
        g.drawHorizontalLine(static_cast<int>(lineY), blockX, blockX + blockW);

        g.setColour(Colors::textDim.withAlpha(0.5f));
        g.setFont(juce::FontOptions(tagFontSize));
        g.drawText(tagline, static_cast<int>(blockX - 10), static_cast<int>(lineY + 6),
                   static_cast<int>(blockW + 20), static_cast<int>(tagFontSize + 6),
                   juce::Justification::centred);
    }
}

// --- Layout ---

void MainComponent::resized()
{
    auto area = getLocalBounds().reduced(16, 10);
    int y = area.getY();
    int w = area.getWidth();

    // Header
    titleLabel.setBounds(area.getX(), y, 100, 22);
    versionLabel.setBounds(area.getX() + 80, y + 6, 60, 16);
    supportButton.setBounds(area.getRight() - 70, y, 70, 22);
    y += 36;

    // Source row
    int detectW = 110;
    sourceLabel.setBounds(area.getX(), y, w - detectW - 8, 20);
    detectButton.setBounds(area.getRight() - detectW, y - 2, detectW, 24);
    y += 36;

    // Progress
    progressBar->setBounds(area.getX(), y, w, 16);
    progressLabel.setBounds(area.getX(), y + 18, w, 16);

    // Status bar + tooltip toggle must be laid out even before the first
    // detection - model-load and download errors land in statusLabel and
    // would otherwise render into a zero-size (invisible) label.
    {
        int statusY = std::max(y + 40, area.getBottom() - 14);
        statusLabel.setBounds(area.getX(), statusY, w - 76, 14);
        tooltipCheckbox.setBounds(area.getRight() - 72, statusY - 2, 72, 16);
    }

    if (!detected_) return;

    // Fixed space for controls below waveform (worst-case: stretch markers options)
    // Using constant so waveform height stays stable when switching action modes
    int controlsH = 6    // gap after waveform
                   + 32   // BPM row
                   + 26   // Action label
                   + 84   // 3 radio buttons (28 * 3)
                   + 90   // worst-case options (stretch: 30+30+30)
                   + 16   // gap before Apply
                   + 30   // Apply button
                   + 38;  // gap + status row
    int statusH = 0;     // status is now relative to Apply, not bottom

    // Waveform fills remaining space (min 100px)
    int waveH = area.getBottom() - y - controlsH - statusH;
    if (waveH < 100) waveH = 100;

    waveformView.setBounds(area.getX(), y, w, waveH);
    y += waveH + 6;

    // Results row 1: BPM with octave buttons + time sig + counts
    bpmRowY_ = y;
    int bx = area.getX();
    bpmLabel.setBounds(bx, y, 36, 24);
    bx += 36;
    bpmHalfBtn.setBounds(bx, y + 2, 24, 20);
    bx += 26;
    bpmEditor.setBounds(bx, y, 70, 24);
    bx += 72;
    bpmDoubleBtn.setBounds(bx, y + 2, 24, 20);
    bx += 28;
    setSessionTempoBtn.setBounds(bx, y + 1, 78, 22);
    bx += 82;
    metronomeBtn.setBounds(bx, y + 1, 32, 22);
    bx += 36;
    timeSigCombo.setBounds(bx, y + 1, 58, 22);
    bx += 62;
    beatCountLabel.setBounds(bx, y + 2, 80, 20);
    confidenceLabel.setBounds(area.getRight() - 44, y + 2, 44, 20);
    bpmOriginalLabel.setBounds(area.getX() + 62, y + 26, 120, 14);
    y += 32;
    if (bpmOriginalLabel.isVisible()) y += 14;

    // Action
    actionLabel.setBounds(area.getX(), y, 100, 20);
    y += 26;

    matchTempoRadio.setBounds(area.getX(), y, 250, 22);
    y += 28;

    if (actionMode_ == kMatchTempo)
    {
        matchRefLabel.setBounds(area.getX() + 24, y + 2, 60, 18);
        matchRefCombo.setBounds(area.getX() + 86, y, w - 86 + area.getX() - 16, 22);
        y += 30;
        targetBpmLabel.setBounds(area.getX() + 24, y + 2, 76, 18);
        targetBpmEditor.setBounds(area.getX() + 100, y, 80, 22);
        y += 30;
        alignCheckbox.setBounds(area.getX() + 24, y, 280, 22);
        y += 28;
    }

    tempoMapRadio.setBounds(area.getX(), y, 250, 22);
    y += 28;

    if (actionMode_ == kTempoMap)
    {
        tempoMapModeCombo.setBounds(area.getX() + 24, y, 200, 22);
        y += 30;
    }

    stretchRadio.setBounds(area.getX(), y, 250, 22);
    y += 28;

    if (actionMode_ == kStretchMarkers)
    {
        markerModeCombo.setBounds(area.getX() + 24, y, 160, 22);
        y += 30;
        quantizeLabel.setBounds(area.getX() + 24, y + 2, 62, 18);
        quantizeCombo.setBounds(area.getX() + 88, y, 150, 22);
        y += 28;
        if (strengthSlider.isVisible())
        {
            strengthLabel.setBounds(area.getX() + 24, y + 2, 62, 18);
            strengthSlider.setBounds(area.getX() + 86, y, w - 86 + area.getX() - 16, 22);
            y += 28;
        }
        stretchQualityCombo.setBounds(area.getX() + 24, y, 160, 22);
        y += 30;
    }

    // Apply (with breathing room above)
    y += 8;
    applyButton.setBounds(area.getX(), y, w, 28);

    // Status (pinned to bottom edge, with minimum gap below Apply)
    int statusY = std::max(y + 32, area.getBottom() - 14);
    statusLabel.setBounds(area.getX(), statusY, w - 76, 14);
    tooltipCheckbox.setBounds(area.getRight() - 72, statusY - 2, 72, 16);
}

// --- Timer: poll selected item ---

void MainComponent::timerCallback()
{
    updateSelectedItem();

    // Keep waveform item position in sync (user may move item on timeline)
    if (detected_ && currentItem_.item && currentItem_.take)
    {
        auto* mi = static_cast<MediaItem*>(currentItem_.item);
        auto* mt = static_cast<MediaItem_Take*>(currentItem_.take);
        double takeOffset = GetMediaItemTakeInfo_Value(mt, "D_STARTOFFS");
        waveformView.setItemInfo(
            GetMediaItemInfo_Value(mi, "D_POSITION"),
            takeOffset,
            GetMediaItemTakeInfo_Value(mt, "D_PLAYRATE"));

        // Poll REAPER stretch markers - update waveform if changed.
        // Skip during ANY active mouse gesture (including the pre-threshold
        // phase between mouseDown and drag commit) - replacing the marker
        // vectors under a pending drag index reads out of bounds. The
        // forced (-1) state persists, so the re-read runs right after
        // mouseUp instead.
        int markerCount = GetTakeNumStretchMarkers(mt);
        bool gestureActive = waveformView.isMouseGestureActive();
        // In marker mode: re-read when forced (-1) or periodically (~1s) for external edits
        // Periodic re-read catches REAPER-side marker edits without oscillation issues
        // In beat mode: re-read on any count change
        if (waveformView.isMarkerEditMode() && !gestureActive)
        {
            if (++markerModeRereadCounter_ >= 5)  // every ~1s (5 * 200ms)
            {
                markerModeRereadCounter_ = 0;
                lastStretchMarkerCount_ = -1;
            }
        }
        else
        {
            markerModeRereadCounter_ = 0;
        }

        bool shouldReread = !gestureActive
            && ((lastStretchMarkerCount_ == -1)
                || (!waveformView.isMarkerEditMode() && markerCount != lastStretchMarkerCount_));

        if (shouldReread)
        {
            lastStretchMarkerCount_ = markerCount;

            // Auto-exit marker mode if all markers removed (e.g., REAPER undo)
            if (waveformView.isMarkerEditMode() && markerCount == 0)
                waveformView.setMarkerEditMode(false);

            std::vector<float> srcPositions;
            std::vector<std::pair<double,double>> pairs;
            srcPositions.reserve(markerCount);
            pairs.reserve(markerCount);
            for (int i = 0; i < markerCount; ++i)
            {
                double pos = 0, srcpos = 0;
                GetTakeStretchMarker(mt, i, &pos, &srcpos);
                // srcpos is a source position and detection times are
                // source-absolute - they share one coordinate space
                float beatTime = static_cast<float>(srcpos);
                if (beatTime >= 0 && beatTime <= detection_.duration)
                    srcPositions.push_back(beatTime);
                pairs.push_back({srcpos, pos});
            }
            std::sort(srcPositions.begin(), srcPositions.end());
            waveformView.setReaperMarkers(srcPositions, pairs);
        }
    }

    // Clear beat flash after 120ms
    if (beatFlashing_)
    {
        uint32_t elapsed = juce::Time::getMillisecondCounter() - beatFlashTime_;
        if (elapsed >= 120)
        {
            beatFlashing_ = false;
            repaint();
        }
    }

    // Update metronome button appearance to reflect REAPER state
    if (GetToggleCommandState)
    {
        bool metOn = GetToggleCommandState(40364) == 1;
        auto targetColour = metOn ? Colors::accent : Colors::buttonBg;
        auto targetText = metOn ? juce::Colour(0xff1e1e1e) : Colors::textDim;

        if (metronomeBtn.findColour(juce::TextButton::buttonColourId) != targetColour)
        {
            metronomeBtn.setColour(juce::TextButton::buttonColourId, targetColour);
            metronomeBtn.setColour(juce::TextButton::textColourOnId, targetText);
        }
    }
}

void MainComponent::updateSelectedItem()
{
    if (!CountSelectedMediaItems || !GetSelectedMediaItem)
        return;

    if (currentItem_.item && ValidatePtr2)
    {
        if (!ValidatePtr2(nullptr, currentItem_.item, "MediaItem*"))
        {
            // Item was deleted in REAPER - keep any manual beat edits in
            // the cache (the same file may be re-inserted later) and reset
            // the UI fully so a dead item name doesn't linger
            if (detected_ && !currentItem_.guid.empty())
                cache_[currentItem_.guid] = detection_;
            currentItem_ = {};
            detected_ = false;
            sourceLabel.setText("Select an audio item in REAPER", juce::dontSendNotification);
            // While detecting, the button is the Cancel button - keep it alive
            detectButton.setEnabled(detecting_);
            waveformView.clear();
            waveformView.setVisible(false);
            showResults(false);
            repaint();
        }
        else if (currentItem_.take
                 && !ValidatePtr2(nullptr, currentItem_.take, "MediaItem_Take*"))
        {
            // Active take deleted/changed while the item stayed selected -
            // refresh instead of dereferencing the dead pointer every tick
            currentItem_.take = GetActiveTake(static_cast<MediaItem*>(currentItem_.item));
            lastStretchMarkerCount_ = -1;
        }
    }

    int count = CountSelectedMediaItems(nullptr);
    if (count <= 0)
    {
        if (currentItem_.item != nullptr)
        {
            // Deselecting must not lose manual beat edits - the item-switch
            // path below saves them, so save here too
            if (detected_ && !currentItem_.guid.empty())
                cache_[currentItem_.guid] = detection_;
            currentItem_ = {};
            detected_ = false;
            sourceLabel.setText("Select an audio item in REAPER", juce::dontSendNotification);
            // While detecting, the button is the Cancel button - keep it alive
            detectButton.setEnabled(detecting_);
            waveformView.clear();
            waveformView.setVisible(false);
            showResults(false);
            repaint();
        }
        return;
    }

    auto* item = GetSelectedMediaItem(nullptr, 0);
    auto* take = GetActiveTake(item);
    if (!take) return;

    // Compare pointer first (fast), then GUID for cache key
    if (item == currentItem_.item)
    {
        // Same item selected - still refresh the take pointer, the user
        // may have switched or deleted the active take ("T" in REAPER)
        if (take != currentItem_.take)
        {
            currentItem_.take = take;
            lastStretchMarkerCount_ = -1;
        }
        return;
    }

    char guidBuf[64] = {};
    if (GetSetMediaItemInfo_String)
        GetSetMediaItemInfo_String(item, "GUID", guidBuf, false);
    std::string guid(guidBuf);

    // Save current state before switching
    if (!currentItem_.guid.empty())
    {
        // Save match reference selection for this item
        int refId = matchRefCombo.getSelectedId();
        if (refId >= 2)
        {
            int idx = refId - 2;
            if (idx >= 0 && idx < static_cast<int>(matchRefGuids_.size()))
                matchRefSelection_[currentItem_.guid] = matchRefGuids_[idx];
        }
        else
            matchRefSelection_[currentItem_.guid] = "";  // project

        if (detected_)
        {
            cache_[currentItem_.guid] = detection_;

            // Preserve the user's BPM correction (/2, x2, typed) across
            // item switches - restoring raw detection_.tempo would silently
            // revert it every time they click away and back
            float savedBpm = bpmEditor.getText().getFloatValue();
            if (savedBpm <= 0) savedBpm = detection_.tempo;

            double firstDbTL = 0;
            if (!detection_.downbeats.empty() && currentItem_.item && currentItem_.take)
            {
                auto* mi = static_cast<MediaItem*>(currentItem_.item);
                auto* mt = static_cast<MediaItem_Take*>(currentItem_.take);
                double iPos = GetMediaItemInfo_Value(mi, "D_POSITION");
                double tOff = GetMediaItemTakeInfo_Value(mt, "D_STARTOFFS");
                double pRate = GetMediaItemTakeInfo_Value(mt, "D_PLAYRATE");
                firstDbTL = iPos + (detection_.downbeats[0] - tOff) / (pRate > 0 ? pRate : 1.0);
            }
            cacheInfo_[currentItem_.guid] = {currentItem_.name, savedBpm,
                                             detection_.confidence, firstDbTL,
                                             timeSigCombo.getSelectedId()};
        }
    }

    currentItem_.item = item;
    currentItem_.take = take;
    currentItem_.guid = guid;
    lastStretchMarkerCount_ = -1; // force re-read of REAPER stretch markers
    currentItem_.duration = static_cast<float>(GetMediaItemInfo_Value(item, "D_LENGTH"));

    auto* source = GetMediaItemTake_Source(take);
    if (source)
    {
        char pathBuf[1024] = {};
        GetMediaSourceFileName(source, pathBuf, sizeof(pathBuf));
        currentItem_.audioPath = pathBuf;

        std::string path(pathBuf);
        auto lastSlash = path.find_last_of("/\\");
        currentItem_.name = (lastSlash != std::string::npos) ? path.substr(lastSlash + 1) : path;
    }

    // Require an actual audio source - MIDI items and in-project sources have empty audioPath.
    bool hasAudio = !currentItem_.audioPath.empty();

    std::string displayName = hasAudio ? currentItem_.name : std::string("[No audio source]");
    if (displayName.length() > 40)
        displayName = displayName.substr(0, 37) + "...";

    int mins = static_cast<int>(currentItem_.duration) / 60;
    int secs = static_cast<int>(currentItem_.duration) % 60;
    char info[128];
    snprintf(info, sizeof(info), "%s  (%d:%02d)", displayName.c_str(), mins, secs);
    sourceLabel.setText(info, juce::dontSendNotification);
    sourceLabel.setColour(juce::Label::textColourId, hasAudio ? Colors::text : Colors::textDim);
    // While detecting, the button acts as Cancel - don't retitle/disable it
    // just because the selection changed under a running detection
    if (!detecting_)
    {
        detectButton.setEnabled(modelLoaded_ && hasAudio);
        if (!hasAudio)
            detectButton.setTooltip("Selected item has no audio source (MIDI or empty)");
        else if (!modelLoaded_)
            detectButton.setTooltip("Model not loaded - check status bar");
        else
            detectButton.setTooltip("Run neural beat detection on the selected audio item");
    }

    auto it = cache_.find(guid);
    if (it != cache_.end())
    {
        detection_ = it->second;
        detected_ = true;
        originalTempo_ = detection_.tempo;

        // Restore the user's corrected BPM if one was saved
        float restoredBpm = detection_.tempo;
        auto infoIt = cacheInfo_.find(guid);
        if (infoIt != cacheInfo_.end() && infoIt->second.tempo > 0)
            restoredBpm = infoIt->second.tempo;

        // The cached downbeats already reflect any meter override, so they
        // double as the Auto snapshot after a restore (the pre-override
        // neural downbeats are not persisted per item)
        autoDownbeats_ = detection_.downbeats;
        autoTimeSigNum_ = detection_.timeSigNum;
        autoTimeSigDenom_ = detection_.timeSigDenom;
        timeSigCombo.setSelectedId(
            infoIt != cacheInfo_.end() ? infoIt->second.timeSigComboId : 1,
            juce::dontSendNotification);
        bpmEditor.setText(juce::String(restoredBpm, 1), false);
        if (std::abs(restoredBpm - detection_.tempo) > 0.05f)
        {
            char was[32];
            snprintf(was, sizeof(was), "(was %.1f)", detection_.tempo);
            bpmOriginalLabel.setText(was, juce::dontSendNotification);
            bpmOriginalLabel.setVisible(true);
        }
        else
        {
            bpmOriginalLabel.setVisible(false);
        }

        waveformView.setData(detection_.peaks, detection_.beats, detection_.downbeats, detection_.duration);
        waveformView.setGridBpm(restoredBpm,
            detection_.beats.empty() ? 0 : detection_.beats[0]);
        {
            auto* mi = static_cast<MediaItem*>(currentItem_.item);
            auto* mt = static_cast<MediaItem_Take*>(currentItem_.take);
            waveformView.setItemInfo(
                GetMediaItemInfo_Value(mi, "D_POSITION"),
                GetMediaItemTakeInfo_Value(mt, "D_STARTOFFS"),
                GetMediaItemTakeInfo_Value(mt, "D_PLAYRATE"));
        }
        waveformView.setVisible(true);
        updateUI();
        setStatus("(cached)", Colors::textDim);
    }
    else
    {
        detected_ = false;
        waveformView.clear();
        waveformView.setVisible(false);
        showResults(false);
    }

    rebuildMatchRefCombo();
    resized();
    repaint();
}

// --- Detection ---

void MainComponent::startDetection()
{
    if (currentItem_.audioPath.empty() || detecting_)
        return;

    detecting_ = true;
    detected_ = false;
    progressValue = 0.0;
    // The button doubles as Cancel while detection runs - the whole
    // pipeline polls shouldCancel, so this is a clean cooperative stop
    detectButton.setButtonText("Cancel");
    detectButton.setTooltip("Cancel the running detection");
    detectButton.setEnabled(true);
    waveformView.setVisible(false);
    showResults(false);

    progressBar->setVisible(true);
    progressLabel.setVisible(true);
    progressLabel.setText("Starting...", juce::dontSendNotification);

    detectionThread_ = std::make_unique<DetectionThread>(
        *this, currentItem_.audioPath, currentItem_.guid, currentItem_.name);
    detectionThread_->startThread();
}

void MainComponent::onDetectionComplete(const DetectionResult& result,
                                        const std::string& forGuid,
                                        const std::string& forName)
{
    detecting_ = false;
    if (detectionThread_)
    {
        // The thread posted this callback as its final statement; wait for
        // run() to actually return before destroying the object
        detectionThread_->stopThread(2000);
        detectionThread_.reset();
    }

    progressBar->setVisible(false);
    progressLabel.setVisible(false);
    detectButton.setButtonText("Detect Beats");
    detectButton.setTooltip("Run neural beat detection on the selected audio item");
    detectButton.setEnabled(modelLoaded_ && !currentItem_.audioPath.empty());

    if (result.error == BeatDetector::kCancelledError)
    {
        setStatus("Detection cancelled", Colors::textDim);
        return;
    }

    if (!result.error.empty())
    {
        setStatus(result.error, Colors::error);
        return;
    }

    // The user may have selected a DIFFERENT item (or none) while detection
    // ran - displaying or caching the result under the current item would
    // attach item A's beats to item B. Cache under the original GUID only.
    if (forGuid != currentItem_.guid)
    {
        if (!forGuid.empty())
        {
            cache_[forGuid] = result;
            cacheInfo_[forGuid] = {forName, result.tempo, result.confidence, 0.0};
            rebuildMatchRefCombo();
        }
        char msg[160];
        snprintf(msg, sizeof(msg),
                 "Detection of '%s' finished (cached) - re-select it to view",
                 forName.c_str());
        setStatus(msg, Colors::warning);
        return;
    }

    detection_ = result;
    detected_ = true;
    originalTempo_ = result.tempo;

    // Snapshot the neural meter so the time-sig dropdown's Auto can
    // restore it after a manual override
    autoDownbeats_ = result.downbeats;
    autoTimeSigNum_ = result.timeSigNum;
    autoTimeSigDenom_ = result.timeSigDenom;
    timeSigCombo.setSelectedId(1, juce::dontSendNotification);

    bpmEditor.setText(juce::String(result.tempo, 1), false);
    bpmOriginalLabel.setVisible(false);

    // Waveform with beat overlay + grid BPM for overlay
    waveformView.setData(result.peaks, result.beats, result.downbeats, result.duration);
    waveformView.setGridBpm(result.tempo,
        result.beats.empty() ? 0 : result.beats[0]);
    waveformView.setVisible(true);

    float projBpm = ReaperActions::getProjectBpm();
    targetBpmEditor.setText(juce::String(projBpm, 1), false);

    // Check filename for BPM hint
    if (!currentItem_.name.empty())
    {
        auto hints = FilenameParser::parse(currentItem_.name);
        if (hints.bpm > 0 && result.tempo > 0)
        {
            float diff = std::abs(hints.bpm - result.tempo) / result.tempo;
            if (diff > 0.05f)  // >5% difference = worth showing
            {
                char hint[64];
                snprintf(hint, sizeof(hint), "(filename: %.0f BPM)", hints.bpm);
                bpmOriginalLabel.setText(hint, juce::dontSendNotification);
                bpmOriginalLabel.setVisible(true);
            }
        }
    }

    // Save to cache info for "Match to:" dropdown
    if (!currentItem_.guid.empty())
    {
        double firstDbTL = 0;
        if (!result.downbeats.empty() && currentItem_.item && currentItem_.take)
        {
            auto* mi = static_cast<MediaItem*>(currentItem_.item);
            auto* mt = static_cast<MediaItem_Take*>(currentItem_.take);
            double iPos = GetMediaItemInfo_Value(mi, "D_POSITION");
            double tOff = GetMediaItemTakeInfo_Value(mt, "D_STARTOFFS");
            double pRate = GetMediaItemTakeInfo_Value(mt, "D_PLAYRATE");
            firstDbTL = iPos + (result.downbeats[0] - tOff) / (pRate > 0 ? pRate : 1.0);
        }
        cacheInfo_[currentItem_.guid] = {currentItem_.name, result.tempo, result.confidence, firstDbTL};
    }
    rebuildMatchRefCombo();

    updateUI();

    char status[128];
    snprintf(status, sizeof(status), "%zu beats, %.1f BPM (beat-this, %.1fs)",
             result.beats.size(), result.tempo, result.detectionTime);
    setStatus(status, Colors::success);

    if (!currentItem_.guid.empty())
    {
        cache_[currentItem_.guid] = result;

        // Evict oldest entries if cache exceeds limit (prevent unbounded memory growth)
        constexpr size_t kMaxCacheEntries = 50;
        if (cache_.size() > kMaxCacheEntries)
        {
            // Keep current item, evict others arbitrarily (unordered_map has no order)
            for (auto it = cache_.begin(); it != cache_.end(); )
            {
                if (it->first != currentItem_.guid && cache_.size() > kMaxCacheEntries)
                    it = cache_.erase(it);
                else
                    ++it;
            }

            // Prune the side maps to the surviving GUIDs, otherwise they
            // grow unboundedly and the "Match to:" dropdown lists evicted
            // items forever (selecting one can only fail)
            for (auto it = cacheInfo_.begin(); it != cacheInfo_.end(); )
            {
                if (cache_.count(it->first) == 0)
                    it = cacheInfo_.erase(it);
                else
                    ++it;
            }
            for (auto it = matchRefSelection_.begin(); it != matchRefSelection_.end(); )
            {
                if (cache_.count(it->first) == 0)
                    it = matchRefSelection_.erase(it);
                else
                    ++it;
            }
            rebuildMatchRefCombo();
        }
    }
}

// --- Apply action ---

void MainComponent::applyAction()
{
    if (!detected_ || !currentItem_.take || !currentItem_.item)
        return;

    auto* take = static_cast<MediaItem_Take*>(currentItem_.take);
    auto* item = static_cast<MediaItem*>(currentItem_.item);

    float userBpm = bpmEditor.getText().getFloatValue();
    if (userBpm <= 0) userBpm = detection_.tempo;

    switch (actionMode_)
    {
        case kMatchTempo:
        {
            float target = targetBpmEditor.getText().getFloatValue();
            if (target <= 0) target = ReaperActions::getProjectBpm();

            int refId = matchRefCombo.getSelectedId();

            if (refId >= 2)
            {
                // --- MULTI-TRACK SYNC PIPELINE ---
                // All steps in one undo block (nested blocks from insertStretchMarkers merge).
                int refIdx = refId - 2;
                std::string refGuid = (refIdx >= 0 && refIdx < static_cast<int>(matchRefGuids_.size()))
                    ? matchRefGuids_[refIdx] : "";

                if (refGuid.empty() || !cache_.count(refGuid) || detection_.downbeats.empty())
                {
                    setStatus("Sync failed: detect both tracks first", Colors::warning);
                    break;
                }

                // Find reference item by GUID BEFORE opening the undo block -
                // a failed lookup must not leave an empty undo point behind
                MediaItem* refItem = nullptr;
                MediaItem_Take* refTake = nullptr;
                int itemCount = CountMediaItems(nullptr);
                for (int i = 0; i < itemCount; ++i)
                {
                    auto* cand = GetMediaItem(nullptr, i);
                    if (!cand) continue;
                    char buf[64] = {};
                    GetSetMediaItemInfo_String(cand, "GUID", buf, false);
                    if (std::string(buf) != refGuid) continue;
                    refItem = cand;
                    refTake = GetActiveTake(cand);
                    break;
                }
                if (!refItem || !refTake)
                {
                    setStatus("Sync failed: reference item not found in project", Colors::warning);
                    break;
                }

                auto& refDet = cache_[refGuid];

                double refPos = GetMediaItemInfo_Value(refItem, "D_POSITION");
                double refOff = GetMediaItemTakeInfo_Value(refTake, "D_STARTOFFS");
                double refRate = GetMediaItemTakeInfo_Value(refTake, "D_PLAYRATE");
                if (refRate <= 0) refRate = 1.0;

                double refDbTL = refPos;
                if (!refDet.downbeats.empty())
                    refDbTL = refPos + (refDet.downbeats[0] - refOff) / refRate;

                // The grid gets the reference's EFFECTIVE tempo (detected
                // tempo scaled by its playrate) - the slave playrate must
                // target the same value or grid and audio disagree by
                // exactly refRate and per-beat stretching has to compensate
                double effectiveBpm = static_cast<double>(refDet.tempo) * refRate;
                double rate = effectiveBpm / userBpm;
                if (rate < 0.25 || rate > 4.0)
                {
                    char warn[128];
                    snprintf(warn, sizeof(warn),
                        "Tempo ratio too extreme: %.1f -> %.1f BPM (%.1fx)",
                        userBpm, effectiveBpm, rate);
                    setStatus(warn, Colors::warning);
                    break;
                }

                if (waveformView.isMarkerEditMode())
                    waveformView.setMarkerEditMode(false);

                Undo_BeginBlock2(nullptr);
                PreventUIRefresh(1);

                // Step 1: Set REAPER grid = reference tempo
                int ex = CountTempoTimeSigMarkers(nullptr);
                for (int j = ex - 1; j >= 0; --j)
                    DeleteTempoTimeSigMarker(nullptr, j);
                SetTempoTimeSigMarker(nullptr, -1, refDbTL, -1, -1,
                    effectiveBpm, refDet.timeSigNum, refDet.timeSigDenom, false);
                UpdateTimeline();

                // Step 2: Stretch markers on REFERENCE (regularize to grid)
                ReaperActions::insertStretchMarkers(
                    refTake, refItem, refDet.beats,
                    4/*project grid*/, 1/*balanced*/,
                    refDet.tempo, refDet.downbeats, refDet.timeSigNum);

                // Step 3: Set slave playrate (pitch preserved) — no separate
                // matchTempo call, everything in one undo block
                double oldRate = GetMediaItemTakeInfo_Value(take, "D_PLAYRATE");
                double oldLen = GetMediaItemInfo_Value(item, "D_LENGTH");
                SetMediaItemTakeInfo_Value(take, "D_PLAYRATE", rate);
                SetMediaItemTakeInfo_Value(take, "B_PPITCH", 1.0);

                // Keep the item covering the SAME source content - deriving
                // length from the full source would extend a trimmed item,
                // revealing trimmed-away audio
                if (oldRate > 0)
                    SetMediaItemInfo_Value(item, "D_LENGTH", oldLen * oldRate / rate);

                // Step 4: Align slave's first downbeat to reference's
                double slavePos = GetMediaItemInfo_Value(item, "D_POSITION");
                double slaveOff = GetMediaItemTakeInfo_Value(take, "D_STARTOFFS");
                double slaveDbTL = slavePos + (detection_.downbeats[0] - slaveOff) / rate;
                double shift = refDbTL - slaveDbTL;
                SetMediaItemInfo_Value(item, "D_POSITION", slavePos + shift);
                UpdateItemInProject(item);

                // Step 5: Stretch markers on SLAVE (project grid)
                int cnt = ReaperActions::insertStretchMarkers(
                    take, item, detection_.beats,
                    4/*project grid*/, 1/*balanced*/,
                    userBpm, detection_.downbeats, detection_.timeSigNum);

                PreventUIRefresh(-1);

                char undoLabel[64];
                snprintf(undoLabel, sizeof(undoLabel),
                    "ReaBeat: Sync to reference (%.1f BPM)", effectiveBpm);
                Undo_EndBlock2(nullptr, undoLabel, -1);

                if (cnt > 0)
                {
                    char msg[128];
                    snprintf(msg, sizeof(msg),
                        "Synced: tempo map + %.1f BPM + %d markers",
                        effectiveBpm, cnt);
                    setStatus(msg, Colors::success);
                    lastStretchMarkerCount_ = -1;
                    waveformView.setMarkerEditMode(true);
                }
                else
                {
                    setStatus("Synced, but no stretch markers inserted - check detection",
                              Colors::warning);
                }
            }
            else
            {
                // --- SIMPLE MATCH TO PROJECT ---
                // Pre-check the ratio here: matchTempo rejects extremes
                // without a dialog (blocking dialogs hide behind the
                // always-on-top window), so the feedback happens here
                double r = static_cast<double>(target) / userBpm;
                if (r < 0.25 || r > 4.0)
                {
                    char warn[128];
                    snprintf(warn, sizeof(warn),
                        "Tempo ratio too extreme: %.1f -> %.1f BPM (%.1fx) - try /2 or x2",
                        userBpm, target, r);
                    setStatus(warn, Colors::warning);
                    break;
                }

                float firstDb = (alignCheckbox.getToggleState() && !detection_.downbeats.empty())
                    ? detection_.downbeats[0] : -1.0f;

                bool ok = ReaperActions::matchTempo(take, item, userBpm, target, firstDb);
                if (ok)
                {
                    char msg[128];
                    snprintf(msg, sizeof(msg),
                        "Matched %.1f -> %.1f BPM (%.2fx)",
                        userBpm, target, target / userBpm);
                    setStatus(msg, Colors::success);
                }
                else
                {
                    setStatus("Match tempo failed - check item selection", Colors::error);
                }
            }
            break;
        }

        case kTempoMap:
        {
            // REAPER tempo markers are quarter-note BPM. In /8 compound
            // meters the model taps dotted quarters, so the detected BPM
            // scales by 1.5 and marker spacing is expressed in quarters
            // (6/8 bar = 3 quarters, 9/8 = 4.5, 12/8 = 6).
            double quartersPerBeat = (detection_.timeSigDenom == 8) ? 1.5 : 1.0;
            double quartersPerBar = detection_.timeSigNum * 4.0
                                  / std::max(1, detection_.timeSigDenom);
            float mapBpm = static_cast<float>(userBpm * quartersPerBeat);

            int modeId = tempoMapModeCombo.getSelectedId();
            std::string mode = "constant";
            std::vector<float> beatList;
            double quartersPerMarker = quartersPerBar;

            if (modeId == 1)
            {
                mode = "constant";
                beatList = detection_.downbeats.empty() ? detection_.beats : detection_.downbeats;
            }
            else if (modeId == 2)
            {
                mode = "variable_bars";
                if (detection_.downbeats.empty())
                {
                    // Falling back to raw beats means ONE beat per marker -
                    // keeping bar spacing would compute localBpm a bar's
                    // worth too high and the 25% tempo filter would then
                    // reject every marker
                    beatList = detection_.beats;
                    quartersPerMarker = quartersPerBeat;
                }
                else
                    beatList = detection_.downbeats;
            }
            else
            {
                mode = "variable_beats";
                beatList = detection_.beats;
                quartersPerMarker = quartersPerBeat;
            }

            int cnt = ReaperActions::insertTempoMap(take, item, mapBpm, beatList,
                quartersPerMarker, detection_.timeSigNum, detection_.timeSigDenom, mode);
            if (cnt > 0)
            {
                char msg[64];
                snprintf(msg, sizeof(msg), "Inserted %d tempo marker(s)", cnt);
                setStatus(msg, Colors::success);
            }
            else
            {
                setStatus("No tempo markers inserted - check detection results", Colors::error);
            }
            break;
        }

        case kStretchMarkers:
        {
            // Exit marker mode before re-applying (Apply replaces ALL markers)
            if (waveformView.isMarkerEditMode())
                waveformView.setMarkerEditMode(false);

            bool downbeatsOnly = markerModeCombo.getSelectedId() == 2;
            auto& beatList = downbeatsOnly ? detection_.downbeats : detection_.beats;
            int quantizeMode = quantizeCombo.getSelectedId();
            // 1 = off, 2 = straight, 3 = bars, 4 = project grid

            // Warn if project grid selected but project tempo doesn't match
            if (quantizeMode == 4)
            {
                float projBpm = ReaperActions::getProjectBpm();
                float ratio = std::abs(projBpm - userBpm) / userBpm;
                if (ratio > 0.10f)
                {
                    char warn[128];
                    snprintf(warn, sizeof(warn),
                        "Session tempo (%.0f) differs from detected (%.0f) - insert tempo map first",
                        projBpm, userBpm);
                    setStatus(warn, Colors::warning);
                }
            }

            int stretchFlags[] = {1, 4, 2};
            int flag = stretchFlags[stretchQualityCombo.getSelectedId() - 1];

            float strength = static_cast<float>(strengthSlider.getValue() / 100.0);
            int cnt = ReaperActions::insertStretchMarkers(take, item, beatList,
                quantizeMode, flag, userBpm,
                detection_.downbeats, detection_.timeSigNum, strength);
            if (cnt > 0)
            {
                static const char* modeNames[] = {"", " (straight)", " (bars)", " (project grid)"};
                char msg[64];
                snprintf(msg, sizeof(msg), "Inserted %d stretch markers%s",
                         cnt, (quantizeMode >= 1 && quantizeMode <= 4) ? modeNames[quantizeMode - 1] : "");
                setStatus(msg, Colors::success);
                lastStretchMarkerCount_ = -1; // force re-read
                waveformView.setMarkerEditMode(true);
            }
            else if (beatList.empty())
            {
                setStatus(downbeatsOnly
                    ? "No downbeats detected - try 'Every beat' mode"
                    : "No beats detected - run detection first",
                    Colors::error);
            }
            else
            {
                setStatus("Failed to insert stretch markers - check item state",
                          Colors::error);
            }
            break;
        }
    }
}

// --- Button callbacks ---

void MainComponent::buttonClicked(juce::Button* button)
{
    if (button == &detectButton)
    {
        if (detecting_)
        {
            // Acting as the Cancel button - signal the thread and let the
            // pipeline's shouldCancel polling wind it down cooperatively
            if (detectionThread_)
                detectionThread_->signalThreadShouldExit();
            detectButton.setEnabled(false);  // re-enabled on completion
            setStatus("Cancelling...", Colors::warning);
            return;
        }
        startDetection();
    }
    else if (button == &applyButton)
        applyAction();
    else if (button == &bpmHalfBtn || button == &bpmDoubleBtn)
    {
        float bpm = bpmEditor.getText().getFloatValue();
        if (bpm <= 0) return;

        bpm = (button == &bpmHalfBtn) ? bpm / 2.0f : bpm * 2.0f;
        // Repeated clicks must not run the BPM out of any musical range -
        // downstream tempo map insertion has no clamp of its own
        if (bpm < 20.0f || bpm > 999.0f) return;
        bpmEditor.setText(juce::String(bpm, 1), false);

        char was[32];
        snprintf(was, sizeof(was), "(was %.1f)", originalTempo_);
        bpmOriginalLabel.setText(was, juce::dontSendNotification);
        bpmOriginalLabel.setVisible(true);
        resized();
    }
    else if (button == &setSessionTempoBtn)
    {
        float bpm = bpmEditor.getText().getFloatValue();
        if (bpm <= 0) bpm = detection_.tempo;

        ReaperActions::setProjectBpm(bpm);

        targetBpmEditor.setText(juce::String(bpm, 1), false);
        rebuildMatchRefCombo();

        char msg[64];
        snprintf(msg, sizeof(msg), "Session tempo set to %.1f BPM", bpm);
        setStatus(msg, Colors::success);
    }
    else if (button == &metronomeBtn)
    {
        if (Main_OnCommand)
            Main_OnCommand(40364, 0);  // Toggle metronome
    }
    else if (button == &supportButton)
    {
        juce::PopupMenu menu;
        menu.addItem(1, "Ko-fi");
        menu.addItem(2, "Buy Me a Coffee");
        menu.addItem(3, "PayPal");
        menu.addSeparator();
        menu.addItem(4, "GitHub");

        // Async: a synchronous modal loop would leave `this` dangling if
        // the window is destroyed while the menu is open (and the item
        // polling timer keeps mutating state under the modal loop)
        menu.showMenuAsync(juce::PopupMenu::Options(), [](int choice)
        {
            juce::URL url;
            switch (choice)
            {
                case 1: url = juce::URL("https://ko-fi.com/quickmd"); break;
                case 2: url = juce::URL("https://buymeacoffee.com/bsroczynskh"); break;
                case 3: url = juce::URL("https://www.paypal.com/paypalme/b451c"); break;
                case 4: url = juce::URL("https://github.com/b451c/ReaBeat"); break;
                default: return;
            }
            url.launchInDefaultBrowser();
        });
    }
}

void MainComponent::comboBoxChanged(juce::ComboBox*) {}

void MainComponent::mouseDown(const juce::MouseEvent& e)
{
    // Click on "ReaBeat" title -> dock/undock + UI scale menu
    if (e.originalComponent == &titleLabel)
    {
        juce::PopupMenu menu;
        bool docked = onIsDocked ? onIsDocked() : false;
        menu.addItem(1, docked ? "Undock window" : "Dock window");

        juce::PopupMenu scaleMenu;
        scaleMenu.addItem(10, "100%", true, uiScale_ < 1.125f);
        scaleMenu.addItem(11, "125%", true, uiScale_ >= 1.125f && uiScale_ < 1.375f);
        scaleMenu.addItem(12, "150%", true, uiScale_ >= 1.375f && uiScale_ < 1.75f);
        scaleMenu.addItem(13, "200%", true, uiScale_ >= 1.75f);
        menu.addSubMenu("UI scale", scaleMenu);

        menu.showMenuAsync(juce::PopupMenu::Options(),
            [safe = juce::Component::SafePointer<MainComponent>(this)](int choice)
        {
            auto* self = safe.getComponent();
            if (!self) return;
            if (choice == 1 && self->onToggleDock)
                self->onToggleDock();
            else if (choice == 10) self->setUiScale(1.0f);
            else if (choice == 11) self->setUiScale(1.25f);
            else if (choice == 12) self->setUiScale(1.5f);
            else if (choice == 13) self->setUiScale(2.0f);
        });
    }
}

void MainComponent::setUiScale(float scale)
{
    uiScale_ = juce::jlimit(1.0f, 2.0f, scale);
    setTransform(uiScale_ == 1.0f ? juce::AffineTransform()
                                  : juce::AffineTransform::scale(uiScale_));
    if (SetExtState)
    {
        char buf[16];
        snprintf(buf, sizeof(buf), "%.2f", uiScale_);
        SetExtState("ReaBeat", "uiscale", buf, true);
    }
    if (onUiScaleChanged)
        onUiScaleChanged();
    resized();
    repaint();
}

// --- UI helpers ---

void MainComponent::setActionMode(ActionMode mode)
{
    actionMode_ = mode;

    matchRefLabel.setVisible(false);
    matchRefCombo.setVisible(false);
    targetBpmLabel.setVisible(false);
    targetBpmEditor.setVisible(false);
    alignCheckbox.setVisible(false);
    tempoMapModeCombo.setVisible(false);
    markerModeCombo.setVisible(false);
    quantizeLabel.setVisible(false);
    quantizeCombo.setVisible(false);
    strengthLabel.setVisible(false);
    strengthSlider.setVisible(false);
    stretchQualityCombo.setVisible(false);

    switch (mode)
    {
        case kMatchTempo:
            matchRefLabel.setVisible(true);
            matchRefCombo.setVisible(true);
            targetBpmLabel.setVisible(true);
            targetBpmEditor.setVisible(true);
            alignCheckbox.setVisible(true);
            break;
        case kTempoMap:
            tempoMapModeCombo.setVisible(true);
            break;
        case kStretchMarkers:
        {
            markerModeCombo.setVisible(true);
            quantizeLabel.setVisible(true);
            quantizeCombo.setVisible(true);
            bool showStrength = quantizeCombo.getSelectedId() > 1;
            strengthLabel.setVisible(showStrength);
            strengthSlider.setVisible(showStrength);
            stretchQualityCombo.setVisible(true);
            break;
        }
    }

    resized();
}

void MainComponent::showResults(bool show)
{
    bpmLabel.setVisible(show);
    bpmHalfBtn.setVisible(show);
    bpmEditor.setVisible(show);
    bpmDoubleBtn.setVisible(show);
    setSessionTempoBtn.setVisible(show);
    metronomeBtn.setVisible(show);
    timeSigCombo.setVisible(show);
    beatCountLabel.setVisible(show);
    confidenceLabel.setVisible(show);
    actionLabel.setVisible(show);
    matchTempoRadio.setVisible(show);
    tempoMapRadio.setVisible(show);
    stretchRadio.setVisible(show);
    applyButton.setVisible(show);

    if (!show)
    {
        bpmOriginalLabel.setVisible(false);
        matchRefLabel.setVisible(false);
        matchRefCombo.setVisible(false);
        targetBpmLabel.setVisible(false);
        targetBpmEditor.setVisible(false);
        alignCheckbox.setVisible(false);
        tempoMapModeCombo.setVisible(false);
        markerModeCombo.setVisible(false);
        quantizeLabel.setVisible(false);
        quantizeCombo.setVisible(false);
        strengthLabel.setVisible(false);
        strengthSlider.setVisible(false);
        stretchQualityCombo.setVisible(false);
    }
    else
    {
        setActionMode(actionMode_);
    }
}

void MainComponent::updateUI()
{
    if (!detected_) return;

    // With Auto selected, show the detected meter as the combo's text
    if (timeSigCombo.getSelectedId() <= 1)
    {
        char ts[16];
        snprintf(ts, sizeof(ts), "%d/%d", detection_.timeSigNum, detection_.timeSigDenom);
        timeSigCombo.setText(juce::String(ts), juce::dontSendNotification);
    }

    int bars = detection_.downbeats.empty() ? 0 : static_cast<int>(detection_.downbeats.size());
    char bc[64];
    snprintf(bc, sizeof(bc), "%zu beats | %d bars", detection_.beats.size(), bars);
    beatCountLabel.setText(bc, juce::dontSendNotification);

    // Color-coded confidence
    int confPct = static_cast<int>(detection_.confidence * 100);
    char conf[16];
    snprintf(conf, sizeof(conf), "%d%%", confPct);
    confidenceLabel.setText(conf, juce::dontSendNotification);

    juce::Colour confColor = confPct >= 80 ? Colors::confHigh
                           : confPct >= 50 ? Colors::confMed
                           : Colors::confLow;
    confidenceLabel.setColour(juce::Label::textColourId, confColor);

    showResults(true);
    resized();
    repaint();
}

void MainComponent::setStatus(const juce::String& msg, juce::Colour colour)
{
    statusLabel.setText(msg, juce::dontSendNotification);
    statusLabel.setColour(juce::Label::textColourId, colour);
}

void MainComponent::applyTimeSigSelection(int comboId)
{
    if (!detected_)
        return;

    struct Meter { int num, den, beatsPerBar; };
    // beatsPerBar counts DETECTED beats per bar: the model taps dotted
    // quarters in compound meters, so 6/8 = 2 main beats, 9/8 = 3, 12/8 = 4
    static const Meter meters[] = {
        {0, 0, 0},                        // index 0 unused (id 1 = Auto)
        {2, 4, 2}, {3, 4, 3}, {4, 4, 4},  // ids 2-4
        {6, 8, 2}, {9, 8, 3}, {12, 8, 4}, // ids 5-7
    };

    if (comboId <= 1)
    {
        // Auto: restore the neural downbeats + detected meter
        detection_.downbeats = autoDownbeats_;
        detection_.timeSigNum = autoTimeSigNum_;
        detection_.timeSigDenom = autoTimeSigDenom_;
    }
    else if (comboId - 1 < static_cast<int>(std::size(meters)))
    {
        const auto& m = meters[comboId - 1];
        detection_.timeSigNum = m.num;
        detection_.timeSigDenom = m.den;

        // Recompute downbeats as every Nth beat, anchored at the beat
        // nearest the current first downbeat (respects manual edits)
        std::vector<float> newDb;
        if (!detection_.beats.empty())
        {
            size_t anchor = 0;
            if (!detection_.downbeats.empty())
            {
                float target = detection_.downbeats[0];
                float bestD = 1e9f;
                for (size_t i = 0; i < detection_.beats.size(); ++i)
                {
                    float d = std::abs(detection_.beats[i] - target);
                    if (d < bestD) { bestD = d; anchor = i; }
                }
            }
            for (size_t i = anchor; i < detection_.beats.size();
                 i += static_cast<size_t>(m.beatsPerBar))
                newDb.push_back(detection_.beats[i]);
        }
        detection_.downbeats = std::move(newDb);
    }
    else
        return;

    if (!currentItem_.guid.empty())
    {
        auto it = cacheInfo_.find(currentItem_.guid);
        if (it != cacheInfo_.end())
            it->second.timeSigComboId = comboId;
        cache_[currentItem_.guid] = detection_;
    }

    waveformView.setData(detection_.peaks, detection_.beats,
                         detection_.downbeats, detection_.duration);
    updateUI();
}

void MainComponent::rebuildMatchRefCombo()
{
    matchRefCombo.clear(juce::dontSendNotification);
    matchRefGuids_.clear();

    // First option: project tempo
    char projLabel[64];
    float projBpm = 0;
    if (GetProjectTimeSignature2)
    {
        double bpm = 0, bpi = 0;
        GetProjectTimeSignature2(nullptr, &bpm, &bpi);
        projBpm = static_cast<float>(bpm);
    }
    snprintf(projLabel, sizeof(projLabel), "Project (%.1f BPM)", projBpm);
    matchRefCombo.addItem(projLabel, 1);

    // Cached reference items (skip current item)
    int id = 2;
    for (auto& [guid, info] : cacheInfo_)
    {
        if (guid == currentItem_.guid) continue;

        // Shorten name for display
        std::string displayName = info.name;
        if (displayName.length() > 28)
            displayName = displayName.substr(0, 25) + "...";

        char label[128];
        snprintf(label, sizeof(label), "%s %.1f BPM %d%%",
                 displayName.c_str(), info.tempo,
                 static_cast<int>(info.confidence * 100));
        matchRefCombo.addItem(label, id);
        matchRefGuids_.push_back(guid);
        ++id;
    }

    // Restore saved selection for this item
    auto selIt = matchRefSelection_.find(currentItem_.guid);
    if (selIt != matchRefSelection_.end() && !selIt->second.empty())
    {
        // Find the saved reference GUID in the rebuilt list
        for (int i = 0; i < static_cast<int>(matchRefGuids_.size()); ++i)
        {
            if (matchRefGuids_[i] == selIt->second)
            {
                matchRefCombo.setSelectedId(i + 2, juce::dontSendNotification);
                return;
            }
        }
    }
    matchRefCombo.setSelectedId(1, juce::dontSendNotification);
}

void MainComponent::loadOrDownloadModel()
{
    auto modelPath = ModelManager::getModelPath();

    if (!modelPath.empty())
    {
        if (beatDetector_.loadModel(modelPath))
        {
            modelLoaded_ = true;
            setStatus("Ready", Colors::textDim);
        }
        else
        {
            setStatus("Failed to load model", Colors::error);
        }
        return;
    }

    // Download asynchronously so UI stays responsive
    setStatus("Downloading model (79 MB)...", Colors::warning);
    detectButton.setEnabled(false);
    detectButton.setButtonText("Downloading model...");
    detectButton.setTooltip("First-run download of the 79 MB neural network model");

    progressValue = 0.0;
    progressBar->setVisible(true);
    progressLabel.setText("Downloading model: 0%", juce::dontSendNotification);
    progressLabel.setVisible(true);
    resized();

    modelDownloadThread_ = std::make_unique<ModelDownloadThread>(*this);
    modelDownloadThread_->startThread();
}

void MainComponent::onModelDownloadComplete(bool ok)
{
    if (modelDownloadThread_)
    {
        // The thread posted this callback as its final statement; wait for
        // run() to actually return before destroying the object
        modelDownloadThread_->stopThread(2000);
        modelDownloadThread_.reset();
    }

    // Always restore button label and hide progress UI after download attempt.
    detectButton.setButtonText("Detect Beats");
    progressBar->setVisible(false);
    progressLabel.setVisible(false);

    if (ok)
    {
        auto modelPath = ModelManager::getModelPath();
        if (!modelPath.empty() && beatDetector_.loadModel(modelPath))
        {
            modelLoaded_ = true;
            setStatus("Model downloaded - Ready", Colors::success);
            bool hasAudio = !currentItem_.audioPath.empty();
            detectButton.setEnabled(!detecting_ && hasAudio);
        }
        else
        {
            setStatus("Model download OK but failed to load", Colors::error);
        }
    }
    else
    {
        setStatus("Download failed. Place beat_this_final0.onnx in ~/.reabeat/models/ or next to the plugin",
                  Colors::error);
    }
}
