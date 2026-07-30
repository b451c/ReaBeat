// ReaBeat Native - Neural beat detection for REAPER
// REAPER extension entry point (JUCE + WDL/SWELL)

#define REAPERAPI_IMPLEMENT
#include "reaper_plugin.h"
#include "reaper_plugin_functions.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include "DockableWindow.h"

#include <memory>

// --- Windows: pre-load onnxruntime.dll from plugin directory ---
// Delay-loaded onnxruntime.dll (see CMakeLists.txt /DELAYLOAD flag).
// Without this, Windows loads System32\onnxruntime.dll (wrong version)
// because UserPlugins is not in the DLL search path.
#ifdef _WIN32
#include <windows.h>
#include <shlwapi.h>
#pragma comment(lib, "shlwapi.lib")

static void preloadOnnxRuntime()
{
    HMODULE hSelf = nullptr;
    GetModuleHandleExW(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        (LPCWSTR)&preloadOnnxRuntime, &hSelf);
    // Without a valid module handle GetModuleFileNameW would return
    // REAPER.exe's path and we'd silently preload from the wrong directory
    // (reintroducing the System32 DLL problem)
    if (!hSelf)
        return;

    wchar_t dllDir[MAX_PATH] = {};
    if (GetModuleFileNameW(hSelf, dllDir, MAX_PATH) == 0)
        return;
    PathRemoveFileSpecW(dllDir);

    wchar_t ortPath[MAX_PATH] = {};
    PathCombineW(ortPath, dllDir, L"onnxruntime.dll");

    // Pre-load from UserPlugins directory so delay-load resolves here
    LoadLibraryW(ortPath);
}
#endif

// --- Globals ---

static std::unique_ptr<DockableWindow> g_window;
static int g_cmdToggle = 0;
static bool g_juceInitialised = false;

// --- JUCE message pump ---
// Pumps JUCE's internal message queue from REAPER's timer.
// Needed for juce::MessageManager::callAsync, juce::Timer, repaint, etc.
// Lightweight no-op when JUCE is not initialised.

static void juceMessagePump()
{
    // Pump ONLY while the plugin window exists and is visible.
    // runDispatchLoopUntil pulls messages from the whole thread queue, so
    // on Windows it can dispatch REAPER's own keyboard events past
    // REAPER's accelerator handling (keys like Space/Esc/F3 appear to need
    // several presses). Running unconditionally meant this kept happening
    // forever after the window was closed - now the pump stops with the
    // window and queued JUCE messages are delivered on next open.
    if (g_juceInitialised && g_window && g_window->isVisible())
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
        g_juceInitialised = true;
    }

    if (!g_window)
    {
        g_window = std::make_unique<DockableWindow>();
        g_window->create();

        if (auto* content = g_window->getContent())
        {
            content->onToggleDock = []() { if (g_window) g_window->toggleDock(); };
            content->onIsDocked = []() -> bool { return g_window && g_window->isDocked(); };
            content->onUiScaleChanged = []() { if (g_window) g_window->relayoutContent(); };
        }
        return true;
    }
    g_window->toggleVisibility();

    return true;
}

// --- Toggle state callback ---

static int toggleActionState(int command)
{
    if (command == g_cmdToggle)
    {
        if (g_window && g_window->isVisible())
            return 1;
        return 0;
    }
    return -1;
}

// --- Cleanup ---

static void onExit()
{
    g_window.reset();

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

#ifdef _WIN32
    // Pre-load correct onnxruntime.dll from UserPlugins before any ORT call.
    // Must happen before JUCE init or any code path that touches ORT.
    preloadOnnxRuntime();
#endif

    // Register toggle action FIRST - bailing out after a timer registration
    // would leave REAPER calling into a DLL that reported failed load
    g_cmdToggle = rec->Register("command_id", (void*)"ReaBeat_ShowWindow");
    if (!g_cmdToggle)
        return 0;

    // Register timer for JUCE message pump (no-op while window hidden)
    rec->Register("timer", (void*)(void(*)())juceMessagePump);

    static gaccel_register_t accel = {{0, 0, 0}, "ReaBeat: Show/Hide Window"};
    accel.accel.cmd = static_cast<unsigned short>(g_cmdToggle);
    rec->Register("gaccel", &accel);

    rec->Register("hookcommand", (void*)onAction);
    rec->Register("toggleaction", (void*)toggleActionState);
    rec->Register("atexit", (void*)onExit);

    return 1;
}

} // extern "C"
