#pragma once

// Dockable window for ReaBeat: SWELL native dialog + embedded JUCE component.
// REAPER's docker system handles both floating and docked states.
// User can dock/undock via REAPER's built-in docker UI (right-click tab, drag).

#include <juce_gui_basics/juce_gui_basics.h>
#include "MainComponent.h"

#include "reaper_plugin.h"
#include "reaper_plugin_functions.h"

#ifndef _WIN32
#include "swell-dlggen.h"
// SWELL uses GWL_/SetWindowLong, not GWLP_/SetWindowLongPtr (Win64 distinction)
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

        content_ = std::make_unique<MainComponent>();
        content_->setSize(500, 660);

        hwnd_ = createNativeDialog(GetMainHwnd(), dlgProc, (LPARAM)this);
        if (!hwnd_) return;

        // Embed JUCE component as child of SWELL view
        content_->addToDesktop(0, (void*)hwnd_);
        content_->setVisible(true);
        onResize();

        // Restore dock state: if previously docked, re-dock; otherwise float
        bool shouldDock = false;
        if (GetExtState)
        {
            const char* docked = GetExtState("ReaBeat", "docked");
            if (docked && docked[0] == '1')
                shouldDock = true;
        }

        if (shouldDock && DockWindowAddEx)
        {
            DockWindowAddEx(hwnd_, "ReaBeat", "ReaBeat_dock", true);
            isDocked_ = true;
        }
        else
        {
            ShowWindow(hwnd_, SW_SHOW);
            isDocked_ = false;
        }
    }

    void destroy()
    {
        if (content_)
        {
            content_->removeFromDesktop();
            content_.reset();
        }
        if (hwnd_)
        {
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
        }
    }

    void toggleDock()
    {
        if (!hwnd_ || !content_) return;
        bool wasDocked = isDocked_;

        // 1. Detach JUCE component first (peer becomes invalid after reparent)
        content_->removeFromDesktop();

        // 2. Remove from docker BEFORE destroying (REAPER holds reference)
        if (wasDocked && DockWindowRemove)
            DockWindowRemove(hwnd_);

        // 3. Destroy old native window
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;

        // Toggle state
        isDocked_ = !wasDocked;
        if (SetExtState)
            SetExtState("ReaBeat", "docked", isDocked_ ? "1" : "0", true);

        // Recreate native window and re-embed JUCE component
        hwnd_ = createNativeDialog(GetMainHwnd(), dlgProc, (LPARAM)this);
        if (!hwnd_) return;

        content_->addToDesktop(0, (void*)hwnd_);
        content_->setVisible(true);
        onResize();

        if (isDocked_ && DockWindowAddEx)
            DockWindowAddEx(hwnd_, "ReaBeat", "ReaBeat_dock", true);
        else
            ShowWindow(hwnd_, SW_SHOW);
    }

    bool isVisible() const
    {
        return hwnd_ && IsWindowVisible(hwnd_);
    }

    bool isDocked() const { return isDocked_; }
    MainComponent* getContent() const { return content_.get(); }

private:
    HWND hwnd_ = nullptr;
    std::unique_ptr<MainComponent> content_;
    bool isDocked_ = false;

    void onResize()
    {
        if (!hwnd_ || !content_) return;
        RECT rc;
        GetClientRect(hwnd_, &rc);
        int w = rc.right - rc.left;
        int h = rc.bottom - rc.top;
        if (w > 0 && h > 0)
            content_->setBounds(0, 0, w, h);
    }

    static INT_PTR CALLBACK dlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        (void)wParam;
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
                self->onResize();
                return 0;

            case WM_CLOSE:
                ShowWindow(hwnd, SW_HIDE);
                return 0;
        }

        return 0;
    }

    // --- Platform-specific native dialog creation ---

#ifdef _WIN32
    static HWND createNativeDialog(HWND parent, DLGPROC proc, LPARAM param)
    {
        #pragma pack(push, 4)
        struct { DLGTEMPLATE tmpl; WORD menu; WORD wndClass; WORD title; } dlg = {};
        #pragma pack(pop)
        dlg.tmpl.style = WS_CHILD | DS_CONTROL;
        dlg.tmpl.cx = 500;
        dlg.tmpl.cy = 660;
        return CreateDialogIndirectParam(GetModuleHandle(nullptr), &dlg.tmpl, parent, proc, param);
    }
#else
    static void noopCreateFunc(HWND, int) {}

    static HWND createNativeDialog(HWND parent, DLGPROC proc, LPARAM param)
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
