#pragma once

// Dockable window for ReaBeat: native dialog + embedded JUCE component.
// Docked: WS_CHILD dialog + DockWindowAddEx (REAPER manages the container)
// Floating: WS_POPUP dialog + ShowWindow (standalone window with frame)
// Linux: SWELL_CreateXBridgeWindow bridges SWELL HWND to X11 Window ID for JUCE

#include <juce_gui_basics/juce_gui_basics.h>
#include "MainComponent.h"

#include "reaper_plugin.h"
#include "reaper_plugin_functions.h"

#ifndef _WIN32
#include "swell-dlggen.h"
#ifndef GWLP_USERDATA
#define GWLP_USERDATA GWL_USERDATA
#endif
#ifndef SetWindowLongPtr
#define SetWindowLongPtr SetWindowLong
#endif
#ifndef GetWindowLongPtr
#define GetWindowLongPtr GetWindowLong
#endif
#endif

class DockableWindow
{
public:
    DockableWindow() = default;
    ~DockableWindow() { destroy(); }

    void create()
    {
        if (hwnd_) return;

        // Check saved dock state
        isDocked_ = false;
        if (GetExtState)
        {
            const char* docked = GetExtState("ReaBeat", "docked");
            if (docked && docked[0] == '1')
                isDocked_ = true;
        }

        createWindow();
    }

    void destroy()
    {
        if (content_)
        {
            content_->removeFromDesktop();
            content_.reset();
        }
#if defined(__linux__) || defined(__FreeBSD__)
        xbridgeHwnd_ = nullptr;
#endif
        if (hwnd_)
        {
            if (isDocked_ && DockWindowRemove)
                DockWindowRemove(hwnd_);
            DestroyWindow(hwnd_);
            hwnd_ = nullptr;
        }
    }

    void toggleVisibility()
    {
        if (!hwnd_)
        {
            create();
            return;
        }

        if (IsWindowVisible(hwnd_))
        {
            ShowWindow(hwnd_, SW_HIDE);
        }
        else
        {
            ShowWindow(hwnd_, SW_SHOW);
            if (isDocked_ && DockWindowActivate)
                DockWindowActivate(hwnd_);
            resizeJuceToFit();
        }
    }

    void toggleDock()
    {
        if (!hwnd_ || !content_) return;

        // Tear down current window
        content_->removeFromDesktop();
        if (isDocked_ && DockWindowRemove)
            DockWindowRemove(hwnd_);
#if defined(__linux__) || defined(__FreeBSD__)
        xbridgeHwnd_ = nullptr;
#endif
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;

        // Toggle
        isDocked_ = !isDocked_;
        if (SetExtState)
            SetExtState("ReaBeat", "docked", isDocked_ ? "1" : "0", true);

        // Recreate in new mode
        createWindow();
    }

    bool isVisible() const { return hwnd_ && IsWindowVisible(hwnd_); }
    bool isDocked() const { return isDocked_; }
    MainComponent* getContent() const { return content_.get(); }

private:
    HWND hwnd_ = nullptr;
    std::unique_ptr<MainComponent> content_;
    bool isDocked_ = false;

#if defined(__linux__) || defined(__FreeBSD__)
    HWND xbridgeHwnd_ = nullptr;
#endif

    void createWindow()
    {
        if (!content_)
        {
            content_ = std::make_unique<MainComponent>();
            content_->setOpaque(true);
        }
        content_->setSize(500, 660);

        hwnd_ = createNativeDialog(GetMainHwnd(), dlgProc, (LPARAM)this, isDocked_);
        if (!hwnd_) return;

        // Set pixel size before embedding
        SetWindowPos(hwnd_, nullptr, 0, 0, 500, 660,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);

        // Embed JUCE as child
        embedJuceComponent();

        if (isDocked_)
        {
            // Docked: register with REAPER's docker
            if (DockWindowAddEx)
                DockWindowAddEx(hwnd_, "ReaBeat", "ReaBeat_dock", true);
        }
        else
        {
            // Floating: just show the popup window
            ShowWindow(hwnd_, SW_SHOW);
        }

        resizeJuceToFit();
    }

    void embedJuceComponent()
    {
#if defined(__linux__) || defined(__FreeBSD__)
        // Linux: SWELL HWND is not an X11 Window - JUCE needs a real X11 Window ID.
        // Use SWELL_CreateXBridgeWindow to create a native X11 child window inside
        // the SWELL dialog, then embed JUCE into that X11 window.
        if (SWELL_CreateXBridgeWindow)
        {
            RECT rc;
            GetClientRect(hwnd_, &rc);
            void* xwindow = nullptr;
            xbridgeHwnd_ = SWELL_CreateXBridgeWindow(hwnd_, &xwindow, &rc);
            if (xbridgeHwnd_ && xwindow)
            {
                content_->addToDesktop(0, xwindow);
                content_->setVisible(true);
                return;
            }
        }
        // Fallback: try direct embedding (may not render on Linux)
        content_->addToDesktop(0, (void*)hwnd_);
#else
        // macOS/Windows: SWELL HWND (NSView*/real HWND) works directly as parent
        content_->addToDesktop(0, (void*)hwnd_);
#endif
        content_->setVisible(true);
    }

    void resizeJuceToFit()
    {
        if (!hwnd_ || !content_) return;
        RECT rc;
        GetClientRect(hwnd_, &rc);
        int w = rc.right - rc.left;
        int h = rc.bottom - rc.top;
        if (w <= 0 || h <= 0) return;

#if defined(__linux__) || defined(__FreeBSD__)
        if (xbridgeHwnd_)
            SetWindowPos(xbridgeHwnd_, NULL, 0, 0, w, h, SWP_NOZORDER | SWP_NOMOVE);
#endif

#ifdef _WIN32
        if (auto* peer = content_->getPeer())
        {
            HWND juceHwnd = (HWND)peer->getNativeHandle();
            if (juceHwnd)
                MoveWindow(juceHwnd, 0, 0, w, h, TRUE);
        }
#else
        content_->setBounds(0, 0, w, h);
#endif
    }

    static INT_PTR CALLBACK dlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        auto* self = reinterpret_cast<DockableWindow*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));

        if (msg == WM_INITDIALOG)
        {
            self = reinterpret_cast<DockableWindow*>(lParam);
            SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)self);
            return 0;
        }

        if (!self) return 0;

        switch (msg)
        {
            case WM_SIZE:
                self->resizeJuceToFit();
                return 0;

            case WM_CLOSE:
                ShowWindow(hwnd, SW_HIDE);
                return 0;

            case WM_DESTROY:
                SetWindowLongPtr(hwnd, GWLP_USERDATA, 0);
                return 0;
        }

        return 0;
    }

#ifdef _WIN32
    // docked=true:  WS_CHILD | DS_CONTROL (for DockWindowAddEx)
    // docked=false: WS_POPUP with frame (standalone floating window)
    static HWND createNativeDialog(HWND parent, DLGPROC proc, LPARAM param, bool docked)
    {
        #pragma pack(push, 4)
        struct { DLGTEMPLATE tmpl; WORD menu; WORD wndClass; WORD title; } dlg = {};
        #pragma pack(pop)

        if (docked)
        {
            dlg.tmpl.style = WS_CHILD | DS_CONTROL;
        }
        else
        {
            dlg.tmpl.style = WS_POPUP | WS_CAPTION | WS_SIZEBOX | WS_SYSMENU
                           | DS_MODALFRAME | WS_VISIBLE;
        }
        dlg.tmpl.cx = 500;
        dlg.tmpl.cy = 660;
        return CreateDialogIndirectParam(
            GetModuleHandle(nullptr), &dlg.tmpl, parent, proc, param);
    }
#else
    static void noopCreateFunc(HWND, int) {}

    static HWND createNativeDialog(HWND parent, DLGPROC proc, LPARAM param, bool /*docked*/)
    {
        static SWELL_DialogResourceIndex res = {
            nullptr, "ReaBeat",
            SWELL_DLG_WS_FLIPPED | SWELL_DLG_WS_OPAQUE | SWELL_DLG_WS_RESIZABLE,
            noopCreateFunc, 500, 660, nullptr
        };
        return SWELL_CreateDialog(&res, nullptr, parent, proc, param);
    }
#endif

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DockableWindow)
};
