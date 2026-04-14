// ReaBeat Native - Neural beat detection for REAPER
// REAPER extension entry point (JUCE + WDL/SWELL)

#define REAPERAPI_IMPLEMENT
#include "reaper_plugin.h"
#include "reaper_plugin_functions.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include "DockableWindow.h"
#include "PluginWindow.h"

#include <memory>

// --- Globals ---

// macOS: dockable SWELL dialog with embedded JUCE component
// Linux/Windows: standalone JUCE window (SWELL/Win32 embedding issues)
#if defined(__APPLE__)
static std::unique_ptr<DockableWindow> g_window;
#else
static std::unique_ptr<PluginWindow> g_pluginWindow;
#endif
static int g_cmdToggle = 0;
static bool g_juceInitialised = false;

// --- JUCE message pump ---

static void juceMessagePump()
{
    if (g_juceInitialised)
        juce::MessageManager::getInstance()->runDispatchLoopUntil(2);
}

// --- Action handler ---

static bool onAction(int command, int)
{
    if (command != g_cmdToggle)
        return false;

    if (!g_juceInitialised)
    {
        juce::initialiseJuce_GUI();
        juce::MessageManager::getInstance()->setCurrentThreadAsMessageThread();
        g_juceInitialised = true;
    }

#if !defined(__APPLE__)
    if (!g_pluginWindow)
    {
        auto* content = new MainComponent();
        g_pluginWindow = std::make_unique<PluginWindow>("ReaBeat", content);
        g_pluginWindow->setVisible(true);
        return true;
    }
    g_pluginWindow->setVisible(!g_pluginWindow->isVisible());
#else
    if (!g_window)
    {
        g_window = std::make_unique<DockableWindow>();
        g_window->create();

        // Wire dock toggle callbacks
        if (auto* content = g_window->getContent())
        {
            content->onToggleDock = []() { if (g_window) g_window->toggleDock(); };
            content->onIsDocked = []() -> bool { return g_window && g_window->isDocked(); };
        }
        return true;
    }
    g_window->toggleVisibility();
#endif

    return true;
}

// --- Toggle state callback ---

static int toggleActionState(int command)
{
    if (command == g_cmdToggle)
    {
#if !defined(__APPLE__)
        if (g_pluginWindow && g_pluginWindow->isVisible())
            return 1;
#else
        if (g_window && g_window->isVisible())
            return 1;
#endif
        return 0;
    }
    return -1;
}

// --- Cleanup ---

static void onExit()
{
#if !defined(__APPLE__)
    g_pluginWindow.reset();
#else
    g_window.reset();
#endif

    if (g_juceInitialised)
    {
        juce::shutdownJuce_GUI();
        g_juceInitialised = false;
    }
}

// --- Entry point ---

extern "C" {

REAPER_PLUGIN_DLL_EXPORT int ReaperPluginEntry(
    HINSTANCE hInstance, reaper_plugin_info_t* rec)
{
    // Cleanup path
    if (!rec)
    {
        onExit();
        return 0;
    }

    // Version check
    if (rec->caller_version != REAPER_PLUGIN_VERSION)
        return 0;

    // Load all REAPER API functions
    if (REAPERAPI_LoadAPI(rec->GetFunc) != 0)
        return 0;

    // Register timer for JUCE message pump (always running, lightweight when JUCE not init)
    rec->Register("timer", (void*)(void(*)())juceMessagePump);

    // Register toggle action
    g_cmdToggle = rec->Register("command_id", (void*)"ReaBeat_ShowWindow");
    if (!g_cmdToggle)
        return 0;

    static gaccel_register_t accel = {{0, 0, 0}, "ReaBeat: Show/Hide Window"};
    accel.accel.cmd = static_cast<unsigned short>(g_cmdToggle);
    rec->Register("gaccel", &accel);

    rec->Register("hookcommand", (void*)onAction);
    rec->Register("toggleaction", (void*)toggleActionState);
    rec->Register("atexit", (void*)onExit);

    return 1;
}

} // extern "C"
