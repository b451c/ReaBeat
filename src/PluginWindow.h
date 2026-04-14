#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

// ReaBeat window: floats above REAPER when REAPER is focused,
// drops to normal level when user switches to another app.
class PluginWindow : public juce::DocumentWindow,
                     private juce::Timer
{
public:
    PluginWindow(const juce::String& name, juce::Component* content)
        : DocumentWindow(name, juce::Colour(0xff1e1e1e), DocumentWindow::closeButton)
    {
        setUsingNativeTitleBar(true);
        setContentOwned(content, true);
        setResizable(true, false);
        setResizeLimits(500, 330, 600, 900);
        centreWithSize(500, 660);
        startTimer(500); // check foreground state every 500ms
    }

    void closeButtonPressed() override
    {
        setVisible(false);
    }

private:
    bool wasOnTop_ = false;

    void timerCallback() override
    {
        if (!isVisible()) return;

        bool shouldBeOnTop = juce::Process::isForegroundProcess();
        if (shouldBeOnTop != wasOnTop_)
        {
            setAlwaysOnTop(shouldBeOnTop);
            wasOnTop_ = shouldBeOnTop;
        }
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginWindow)
};
