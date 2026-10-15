# Nexora

A small floating application launcher for Windows. Press **Ctrl+Space**, type a few letters, press **Enter**, and you're back to work.

- Compact frosted-glass window (native Windows 11 acrylic, rounded corners, DWM shadow) over a dimmed desktop
- Real application icons, including the unplated artwork of Store apps
- Instant case-insensitive search: prefix, word-start and initials (`vsc` → *Visual Studio Code*)
- Full keyboard control, single-click launching, Escape or click outside to dismiss
- Lives in the tray; no administrator rights needed; nothing is ever executed during scanning

## Install

Pick one from `dist\` (see [Build](#build) to produce them):

- **`Nexora-1.0.0-x64.msi`**
  - Per-user installer. It needs no administrator rights and installs to `%LOCALAPPDATA%\Programs\Nexora`.
  - It adds a Start menu shortcut and starts Nexora with Windows. Turn that off from the tray menu; upgrades and repairs keep your choice.
  - If a "Start with Windows" entry already exists (for example for a portable copy), installing leaves it alone.
  - A running Nexora is closed automatically during installs, upgrades and uninstalls, including a portable copy that happens to be running.
  - Reinstalling a rebuilt package of the same version replaces the existing install.
  - It appears in *Settings → Apps*, and uninstalling removes everything it added.
  - Your settings in `%LOCALAPPDATA%\Nexora` are kept on uninstall.
- **`Nexora-1.0.0-portable-x64.zip`**
  - Unzip anywhere and run `Nexora.exe`.

The packages are not code-signed, so Windows SmartScreen may ask for confirmation the first time.

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
tools\build.cmd                    :: release build + tests + dist\Nexora
tools\build.cmd debug
tools\build.cmd release package    :: also the portable zip and the MSI
```

This command:
1. Loads the MSVC environment, configures and builds.
2. Runs the headless tests.
3. Deploys a self-contained copy to **`dist\Nexora\`** (`Nexora.exe` plus the Qt runtime, about 28 MB).

With `package`, it also writes `dist\Nexora-<version>-portable-x64.zip`. If [WiX Toolset v3](https://wixtoolset.org/docs/wix3/) is installed, it writes `dist\Nexora-<version>-x64.msi` too.

**Visual Studio:**
1. *File → Open → Folder…*, select the repository, and choose the `x64 Release` preset.
2. Build `Nexora.exe`.
3. Build the `deploy`, `portable` or `installer` targets for distribution.

**Manually** (from a *x64 Native Tools* prompt):

```bat
cmake --preset release
cmake --build --preset release
ctest --preset release              :: all tests (the UI test briefly opens a window)
cmake --build --preset release --target deploy
cmake --build --preset release --target portable installer
```

### Tests

`tests/` holds Qt Test suites run by CTest. They link the same `NexoraCore` library as the app.

| Suite | Covers |
|---|---|
| `tst_search` | Case-insensitive filtering, prefix/word-start/initials ranking, multi-term queries, hidden apps |
| `tst_hotkey` | Shortcut validation, registration conflicts, release |
| `tst_cache` | Round trip including icons and Unicode; corrupt, outdated and out-of-range files |
| `tst_discovery` | Custom-folder depth and noise filtering, `.url` handling, Start menu discovery, cancellation, icon extraction (desktop and packaged) |
| `tst_launcher` | Elevation rules, error reporting, async result delivery and lifetime safety |
| `tst_watcher` | Recursive change detection, ignoring unrelated writes and the data folder, subfolders stay renameable |
| `tst_ui` | The real launcher window: typing, keyboard navigation, column layout, hover highlight, `+` menu, context menu, empty/loading states, Enter and Esc. It saves snapshots to `out/build/<preset>/tests/snapshots`. Mouse input goes through Qt's window-level injection, so the OS cursor is never moved. If you click elsewhere during the run, the launcher dismisses itself and the remaining checks are skipped. |

Tests use a temporary data folder (`NEXORA_DATA_DIR`), so they never touch your real settings or cache. `tst_ui` is labelled `ui`; skip it with `ctest -LE ui`.

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

The application cache (names plus icons) is `%LOCALAPPDATA%\Nexora\apps.cache`. Deleting it is always safe. Set the `NEXORA_DATA_DIR` environment variable to keep settings and cache in another folder, for example next to a portable copy.

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
| Theme | `Theme` | Palette, fonts, rounded translucent menus (including Qt's built-in ones) |
| Folder watching | `FolderWatcher` | Recursive, non-locking change notifications for the Start menu and custom folders |
| Wiring | `NexoraApp`, `main` | Tray menu, single instance, rescan scheduling |
| Packaging | `packaging/make_installer.cmake` | Generates and builds the per-user WiX MSI from `dist\Nexora` |

**Discovery sources**, in priority order for deduplication:

1. Manually added `.exe`/`.lnk` files
2. User-selected folders: the folder plus two levels of subfolders, breadth-first, without following links, capped at 4,000 entries
3. Start menu shortcuts for the current user and all users:
   - `.lnk` shortcuts are resolved with `IShellLink`, so arguments are preserved.
   - `.url` files are included when they use an app protocol (Steam, Epic, …) rather than a web link.
   - ClickOnce `.appref-ms` files are included.
   - Uninstallers, readmes, broken links and non-programs are skipped.
4. Packaged / Microsoft Store apps from the shell Apps folder (AUMIDs). Logos are read from each package's manifest, using the same unplated asset Start uses.
5. Uninstall registry entries (HKCU and HKLM, 64- and 32-bit). An entry is used only if its `DisplayIcon` points to a real program and it isn't already listed.

Entries are deduplicated by target executable plus arguments, and by name for the secondary sources. Discovery never crawls whole drives and never runs anything; scans stop promptly when Nexora quits.

On start the cached list appears immediately, and a fresh scan runs in the background. Nexora rescans when:
- the Start menu folders or your custom folders change. These are watched live, with one recursive `ReadDirectoryChangesW` handle per folder tree that never locks the folders inside it.
  - Only changes that could affect an app count: programs and shortcuts added, changed, renamed or removed, and folders added, renamed or removed.
  - Writes to and deletions of other files (logs, caches) are ignored, as are changes deeper inside a custom folder than discovery scans, and Nexora's own data folder.
  - A watched folder that is missing (for example on a drive that isn't mounted yet) or gets deleted is retried every minute.
  - The rescan waits about 2.5 s after the last change so installers settle first;
- the launcher is opened and the data is older than 10 minutes. Background scans re-extract icons only for new entries, or when a shortcut or the program it points to has changed. `+` → *Refresh applications* rescans and re-extracts every icon. A cold scan of about 160 apps takes about 5 s on the development machine; the grid appears before icons finish streaming in.

**Frosted glass:** the launcher is a regular (non-layered) top-level window. Its native frame is folded into the client area with Qt's `QWindowsWindow::setCustomMargins`, so DWM still provides the shadow, Windows 11 rounded corners, open/close transitions and the `DWMSBT_TRANSIENTWINDOW` acrylic backdrop. The background artwork is painted over it at partial opacity under a dark navy overlay, so the blurred desktop shows through. While the launcher is open every monitor is dimmed, and a click on any of them dismisses it without reaching the window underneath.

**Footprint:**
- **Idle memory:** between uses Nexora unloads the shell extension DLLs that icon extraction pulls in. It also trims its working set after each scan and each dismiss, to about 1 MB resident (about 40 MB private).
- **Warm-up:** started with `--background`, Nexora pre-renders the launcher off-screen. This halved the first open on the development machine, to about 0.2 s; later opens take about 0.1 s.
- **Trade-off:** the trimmed pages come back as soft faults when the launcher next opens, which is cheap. Under heavy memory pressure they can be hard faults, and that open is slower.

## Known limitations

- **Qt private API.** `setCustomMargins` lives in `Qt6::GuiPrivate`. It is stable in practice but not covered by Qt's compatibility promise, so it may need a touch-up on future Qt versions.
- **Windows 10** falls back to the legacy acrylic accent (`SetWindowCompositionAttribute`) and has square corners. This path compiles but has only been tested on Windows 11. If no blur is available at all, an opaque gradient is painted instead.
- **Ctrl+Space** is also used by some input-method editors (e.g. Chinese IMEs). If registration fails, Nexora shows a tray notification; pick another shortcut from the tray menu. A shortcut must include Ctrl, Alt or Win, since a global hotkey takes its key away from every other app. Keys that only exist on non-Latin keyboard layouts may be rejected.
- Registry discovery is heuristic. Occasional helper programs can slip through; hide them with a right-click.
- *Run as administrator* and *Open file location* are not offered for packaged apps, and *Run as administrator* is not offered for `.url` or ClickOnce entries.
- Apps installed only through the uninstall registry, with no Start menu shortcut, are picked up on the next stale or manual refresh. The registry is not watched.
- The MSI and the executable are not code-signed. Signing needs a code-signing certificate (`signtool` from the Windows SDK can apply one).
- **An uninstall that fails or is cancelled part-way** is rolled back. Nexora keeps working and your "Start with Windows" setting is restored.
  - On the development machine (Windows 11 22H2), a forced failure showed that Windows Installer's own rollback of a per-user uninstall is incomplete. The same happened with a package stripped of every custom action, so it is not specific to Nexora's package.
  - Afterwards the product could not be repaired, and the next uninstall left the files and Start menu shortcut behind.
  - To recover, install the same `.msi` file again, then uninstall it; that removed everything in testing.
- Uninstalling removes the "Start with Windows" entry only when it points at the installed copy, so a portable copy's autostart is left alone.
- The MSI is built with WiX Toolset v3. WiX v4/v5 use a different command line and are not supported by `packaging/make_installer.cmake`.

## Assets

`resources/logo.png` and `resources/nexora.ico` are generated by `python tools/make_icon.py` (needs Pillow). `resources/background.jpg` is the gradient artwork painted under the dark overlay.
