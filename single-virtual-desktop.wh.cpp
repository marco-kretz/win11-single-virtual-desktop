// ==WindhawkMod==
// @id              single-virtual-desktop
// @name            Single Virtual Desktop
// @description     GNOME-style workspaces: only the primary monitor switches virtual desktops, windows on other monitors stay on every desktop
// @version         1.0
// @author          Marco Kretz
// @github          https://github.com/marco-kretz
// @include         explorer.exe
// @architecture    x86-64
// @compilerOptions -lole32 -luuid -ldwmapi
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
# Single Virtual Desktop

Like GNOME's "Workspaces on primary display only": switching virtual desktops only changes the primary monitor.

Every window that has fully left the primary monitor is shown on all desktops ("Show this window on all desktops").
Once any part of it is back on the primary monitor, it belongs to the current desktop again.
Windows you pinned yourself are left alone, and disabling the mod unpins everything it pinned.
*/
// ==/WindhawkModReadme==

#include <windows.h>
#include <dwmapi.h>
#include <objectarray.h>
#include <servprov.h>
#include <algorithm>
#include <vector>

// Undocumented shell interfaces behind "Show this window on all desktops"; these GUIDs and
// vtable layouts have been stable since Windows 10 (same ones VirtualDesktopAccessor uses).
const CLSID CLSID_ImmersiveShell = {0xc2f03a33, 0x21f5, 0x47fa, {0xb4, 0xbb, 0x15, 0x63, 0x62, 0xa2, 0xf2, 0x39}};
const IID IID_IApplicationViewCollection = {0x1841c6d7, 0x4f9d, 0x42c0, {0xaf, 0x41, 0x87, 0x47, 0x53, 0x8f, 0x10, 0xe5}};
const CLSID CLSID_VirtualDesktopPinnedApps = {0xb5a399e7, 0x1c87, 0x46b8, {0x88, 0xe9, 0xfc, 0x57, 0x47, 0xb1, 0x71, 0xbd}};
const IID IID_IVirtualDesktopPinnedApps = {0x4ce81583, 0x1e4c, 0x4632, {0xa6, 0x21, 0x07, 0xa5, 0x35, 0x43, 0x14, 0x8f}};

// IApplicationView's own IID changes between builds, so views are only passed around as IUnknown.
struct IApplicationViewCollection : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetViews(IObjectArray**) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetViewsByZOrder(IObjectArray**) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetViewsByAppUserModelId(PCWSTR, IObjectArray**) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetViewForHwnd(HWND, IUnknown**) = 0;
};

struct IVirtualDesktopPinnedApps : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE IsAppIdPinned(PCWSTR, BOOL*) = 0;
    virtual HRESULT STDMETHODCALLTYPE PinAppID(PCWSTR) = 0;
    virtual HRESULT STDMETHODCALLTYPE UnpinAppID(PCWSTR) = 0;
    virtual HRESULT STDMETHODCALLTYPE IsViewPinned(IUnknown*, BOOL*) = 0;
    virtual HRESULT STDMETHODCALLTYPE PinView(IUnknown*) = 0;
    virtual HRESULT STDMETHODCALLTYPE UnpinView(IUnknown*) = 0;
};

HANDLE g_stop, g_thread;
IApplicationViewCollection* g_views;
IVirtualDesktopPinnedApps* g_pins;
std::vector<HWND> g_pinned;  // only windows we pinned, so the user's own pins survive

bool OffPrimaryMonitor(HWND h) {
    // The visible frame, without the invisible resize borders that overlap a neighbouring monitor when maximized.
    RECT r, overlap;
    if (DwmGetWindowAttribute(h, DWMWA_EXTENDED_FRAME_BOUNDS, &r, sizeof(r)) != S_OK) GetWindowRect(h, &r);
    MONITORINFO mi{sizeof(mi)};
    GetMonitorInfoW(MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY), &mi);
    return !IntersectRect(&overlap, &r, &mi.rcMonitor);
}

bool InMoveSize(HWND h) {
    GUITHREADINFO gi{sizeof(gi)};
    return GetGUIThreadInfo(GetWindowThreadProcessId(h, nullptr), &gi) && (gi.flags & GUI_INMOVESIZE);
}

void SetPinned(HWND h, bool pin) {
    IUnknown* view;
    if (g_views->GetViewForHwnd(h, &view) != S_OK) return;  // not a taskbar-style app window
    if (pin) {
        BOOL already = FALSE;
        g_pins->IsViewPinned(view, &already);
        if (!already && g_pins->PinView(view) == S_OK) g_pinned.push_back(h);
    } else {
        g_pins->UnpinView(view);
        std::erase(g_pinned, h);
    }
    view->Release();
}

void Update(HWND h) {
    // Minimized windows sit at -32000 and keep their state; dragged windows are judged at EVENT_SYSTEM_MOVESIZEEND.
    if (!IsWindowVisible(h) || IsIconic(h) || GetAncestor(h, GA_ROOT) != h || GetWindow(h, GW_OWNER) || InMoveSize(h))
        return;
    bool want = OffPrimaryMonitor(h);
    if (want != (std::find(g_pinned.begin(), g_pinned.end(), h) != g_pinned.end())) SetPinned(h, want);
}

void CALLBACK OnWinEvent(HWINEVENTHOOK, DWORD event, HWND h, LONG obj, LONG child, DWORD, DWORD) {
    if (!h || obj != OBJID_WINDOW || child != CHILDID_SELF) return;
    if (event == EVENT_OBJECT_DESTROY) std::erase(g_pinned, h);
    else Update(h);
}

bool AcquireServices() {
    IServiceProvider* shell;
    if (CoCreateInstance(CLSID_ImmersiveShell, nullptr, CLSCTX_ALL, IID_IServiceProvider, (void**)&shell) != S_OK)
        return false;
    shell->QueryService(IID_IApplicationViewCollection, IID_IApplicationViewCollection, (void**)&g_views);
    shell->QueryService(CLSID_VirtualDesktopPinnedApps, IID_IVirtualDesktopPinnedApps, (void**)&g_pins);
    shell->Release();
    if (g_views && g_pins) return true;
    if (g_views) g_views->Release();
    if (g_pins) g_pins->Release();
    g_views = nullptr;
    g_pins = nullptr;
    return false;
}

void Run() {
    // ponytail: no WM_DISPLAYCHANGE rescan; moving/changing monitors already fires location changes for the windows.
    HWINEVENTHOOK hooks[] = {
        SetWinEventHook(EVENT_SYSTEM_MOVESIZEEND, EVENT_SYSTEM_MOVESIZEEND, nullptr, OnWinEvent, 0, 0,
                        WINEVENT_OUTOFCONTEXT),
        SetWinEventHook(EVENT_OBJECT_DESTROY, EVENT_OBJECT_SHOW, nullptr, OnWinEvent, 0, 0, WINEVENT_OUTOFCONTEXT),
        SetWinEventHook(EVENT_OBJECT_LOCATIONCHANGE, EVENT_OBJECT_LOCATIONCHANGE, nullptr, OnWinEvent, 0, 0,
                        WINEVENT_OUTOFCONTEXT),
    };
    EnumWindows([](HWND h, LPARAM) { Update(h); return TRUE; }, 0);

    while (MsgWaitForMultipleObjects(1, &g_stop, FALSE, INFINITE, QS_ALLINPUT) != WAIT_OBJECT_0) {
        MSG m;
        while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) DispatchMessageW(&m);
    }

    for (HWINEVENTHOOK hook : hooks)
        if (hook) UnhookWinEvent(hook);
    for (HWND h : std::vector<HWND>(g_pinned))
        if (IsWindow(h)) SetPinned(h, false);
    g_pinned.clear();
    g_views->Release();
    g_pins->Release();
}

DWORD WINAPI WorkerThread(void*) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    // The shell services appear some time after Explorer loads the mod.
    while (WaitForSingleObject(g_stop, 0) == WAIT_TIMEOUT) {
        HWND tray = FindWindowW(L"Shell_TrayWnd", nullptr);
        DWORD pid = 0;
        if (tray) GetWindowThreadProcessId(tray, &pid);
        if (tray && pid != GetCurrentProcessId()) break;  // a separate file explorer process; the shell lives elsewhere
        if (tray && AcquireServices()) {
            Run();
            break;
        }
        WaitForSingleObject(g_stop, 1000);
    }
    CoUninitialize();
    return 0;
}

BOOL Wh_ModInit() {
    g_stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    g_thread = CreateThread(nullptr, 0, WorkerThread, nullptr, 0, nullptr);
    return TRUE;
}

void Wh_ModUninit() {
    SetEvent(g_stop);
    WaitForSingleObject(g_thread, INFINITE);
    CloseHandle(g_thread);
    CloseHandle(g_stop);
}
