# Single Virtual Desktop

GNOME-style "workspaces on primary display only" for Windows 11, as a [Windhawk](https://windhawk.net/) mod.

## The problem

Windows switches virtual desktops on every monitor at once. If you keep chat, music or docs on a secondary monitor, they disappear whenever you switch desktops on your main screen. Windows has no setting to limit desktops to the primary monitor.

## What it does

Switching virtual desktops only changes the primary monitor. Secondary monitors stay as they are.

- A window that has **fully left** the primary monitor is shown on all desktops (like "Show this window on all desktops").
- As soon as **any part** of it is back on the primary monitor, it belongs to the current desktop again.
- Windows you pinned to all desktops yourself are left alone.
- Disabling the mod unpins every window it pinned.

## Install

1. Install [Windhawk](https://windhawk.net/).
2. In Windhawk, click **Create a new mod**, replace the code with the contents of [`single-virtual-desktop.wh.cpp`](single-virtual-desktop.wh.cpp), then **Compile** and **Enable**.

The mod runs inside `explorer.exe`.

## Recommended

- [DesktopDots](https://github.com/marco-kretz/win11-desktop-dots): a GNOME-style virtual desktop indicator on the taskbar.
- [Disable Virtual Desktop Transition](https://windhawk.net/mods/disable-virtual-desktop-transition): removes the slide animation, so switching desktops is basically instant.

## How it works

- A worker thread in `explorer.exe` waits for the shell, then listens for window events via `SetWinEventHook` (show, move/resize end, location change, destroy). There is no polling.
- For each top-level window it compares the visible frame (`DWMWA_EXTENDED_FRAME_BOUNDS`, so the invisible resize borders of a maximized window don't count) with the primary monitor.
- Windows being dragged are judged when the drag ends; minimized windows keep their state.
- Pinning uses the **undocumented** shell interfaces `IApplicationViewCollection` and `IVirtualDesktopPinnedApps` (the same ones [VirtualDesktopAccessor](https://github.com/Ciantic/VirtualDesktopAccessor) uses). There is no public API for "Show this window on all desktops".
- The mod remembers which windows it pinned itself, so it only ever unpins those.

## Caveats

- **Only tested on my own machine.** Other monitor setups, taskbar tweaks and Windows builds are untested.
- Relies on undocumented shell interfaces, which a Windows update could change.
- Only windows that show up in the taskbar/Alt+Tab can be pinned; tool windows and owned popups are ignored.

Feel free to take it, change it and redistribute it.

## License

[MIT](LICENSE)
