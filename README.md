# Nexora

A small floating application launcher for Windows. Press **Ctrl+Space**, type a few letters, press **Enter**, and you're back to work.

- Compact frosted-glass window (native Windows 11 acrylic, rounded corners, DWM shadow) over a dimmed desktop
- Real application icons, including the unplated artwork of Store apps
- Instant case-insensitive search: prefix, word-start and initials (`vsc` → *Visual Studio Code*)
- Full keyboard control, single-click launching, Escape or click outside to dismiss
- Lives in the tray; no administrator rights needed; nothing is ever executed during scanning

## Requirements

| | |
|---|---|
| OS | Windows 10 1809+ (best on Windows 11 22H2+) |
| Compiler | Visual Studio 2022 or later with *Desktop development with C++* |
| Qt | Qt 6.5+ for MSVC x64 (developed against **6.10.3**) |
| CMake | 3.21+ (Ninja ships with Visual Studio) |

## Build

The Qt location is set in `CMakePresets.json` (`CMAKE_PREFIX_PATH`, currently `D:/SDKs/Qt/6.10.3/msvc2022_64`). Change it there, or override it in a `CMakeUserPresets.json`, if your Qt lives elsewhere.

**One command** (from any shell):

```bat
tools\build.cmd            :: release
tools\build.cmd debug
```

This loads the MSVC environment, configures, builds, and deploys a self-contained copy to **`dist\Nexora\`** (`Nexora.exe` plus the Qt runtime, about 28 MB).

**Visual Studio:** *File → Open → Folder…*, select the repository, choose the `x64 Release` preset, and build `Nexora.exe`. Build the `deploy` target to refresh `dist\Nexora`.

**Manually** (from a *x64 Native Tools* prompt):

```bat
cmake --preset release
cmake --build --preset release
cmake --build --preset release --target deploy
```

`dist\Nexora` uses the Microsoft Visual C++ runtime that is already installed on almost every Windows PC. If it is missing, install the [VC++ Redistributable](https://aka.ms/vs/17/release/vc_redist.x64.exe).

## Usage

| Action | How |
|---|---|
| Open / close | `Ctrl+Space` (configurable), the tray icon, or start `Nexora.exe` again |
| Search | Just type; the grid filters as you type |
| Move selection | `↑ ↓ ← →`, `Tab`, `Shift+Tab`, `PgUp`, `PgDn` |
| Launch | `Enter` or click |
| Launch as administrator | `Ctrl+Shift+Enter`, or right-click → *Run as administrator* |
| Dismiss | `Esc`, the × button, or click anywhere outside |
| Add an app or a folder of portable apps | `+` button, or the tray menu |
| Hide a noisy entry | Right-click → *Hide from Nexora* (undo: tray → *Show hidden applications*) |

`Nexora.exe --background` starts in the tray without opening the window. The tray menu's *Start with Windows* option uses this.

### Settings

Settings are stored in `%LOCALAPPDATA%\Nexora\nexora.ini`, and most are changed from the tray menu:

```ini
hotkey=Ctrl+Space
dimDesktop=true
customApps=C:\\Tools\\thing.exe
customFolders=D:\\PortableApps
hiddenApps=app:c:/windows/system32/...|
```

The application cache (names plus icons) is `%LOCALAPPDATA%\Nexora\apps.cache`. Deleting it is always safe.

## How it works

| Component | File | Responsibility |
|---|---|---|
| Launcher window | `LauncherWindow` | Search field, grid, empty/loading states, keyboard handling, show/hide |
| Visual effects | `WindowEffects`, `DimOverlay`, `Spinner` | Acrylic backdrop, frame removal, desktop dimming, loading indicator |
| Grid and search | `AppModel`, `AppDelegate` | List model, relevance-ranked filter model, tile painting |
| Discovery | `AppDiscovery`, `DiscoveryService`, `AppCache` | Scans sources on a background COM thread, streams icons, caches results |
| Launching | `AppLauncher` | `ShellExecuteEx` / `IApplicationActivationManager`, off the UI thread |
| Global shortcut | `GlobalHotkey` | `RegisterHotKey` on a message-only window; also receives "show" from a second instance |
| Settings | `Settings` | INI file plus the HKCU `Run` value |
| Wiring | `NexoraApp`, `main` | Tray menu, single instance, theme |

**Discovery sources**, in priority order for deduplication:

1. Manually added `.exe`/`.lnk` files
2. User-selected folders (scanned two levels deep)
3. Start menu shortcuts for the current user and all users. Shortcuts are resolved with `IShellLink`, so arguments are preserved. Uninstallers, readmes, broken links and non-programs are skipped.
4. Packaged / Microsoft Store apps from the shell Apps folder (AUMIDs). Logos are read from each package's manifest, using the same unplated asset Start uses.
5. Uninstall registry entries (HKCU and HKLM, 64- and 32-bit). An entry is used only if its `DisplayIcon` points to a real program and it isn't already listed.

Entries are deduplicated by target executable plus arguments, and by name for the secondary sources. Discovery never recurses through drives and never runs anything. On start the cached list appears immediately, and a fresh scan runs in the background. Opening the launcher triggers another scan once the data is older than 10 minutes; `+` → *Refresh applications* rescans at any time. Icon extraction runs only for new or changed entries. A cold scan of about 160 apps takes about 5 s on the development machine; the grid appears before icons finish streaming in.

**Frosted glass:** the launcher is a regular (non-layered) top-level window. Its native frame is folded into the client area with Qt's `QWindowsWindow::setCustomMargins`, so DWM still provides the shadow, Windows 11 rounded corners, open/close transitions and the `DWMSBT_TRANSIENTWINDOW` acrylic backdrop. The background artwork is painted over it at partial opacity under a dark navy overlay, so the blurred desktop shows through.

## Known limitations

- **Qt private API.** `setCustomMargins` lives in `Qt6::GuiPrivate`. It is stable in practice but not covered by Qt's compatibility promise, so it may need a touch-up on future Qt versions.
- **Windows 10** falls back to the legacy acrylic accent (`SetWindowCompositionAttribute`) and has square corners. This path compiles but has only been tested on Windows 11. If no blur is available at all, an opaque gradient is painted instead.
- **Ctrl+Space** is also used by some input-method editors (e.g. Chinese IMEs). If registration fails, Nexora shows a tray notification; pick another shortcut from the tray menu.
- The desktop is dimmed only on the monitor where the launcher opens. Clicking another monitor still dismisses the launcher, but that click also reaches the window under it.
- New Start menu entries are not watched live. They appear on the next stale refresh (10 minutes) or after *Refresh applications*.
- Registry discovery is heuristic. Occasional helper programs can slip through; hide them with a right-click.
- *Run as administrator* and *Open file location* are not offered for packaged apps.
- Memory use is around 110 MB, mostly from shell extensions the icon APIs load into the process.
- There is no installer. `dist\Nexora` is a portable folder.

## Assets

`resources/logo.png` and `resources/nexora.ico` are generated by `python tools/make_icon.py` (needs Pillow). `resources/background.jpg` is the gradient artwork painted under the dark overlay.
