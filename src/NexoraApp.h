#pragma once

#include "DiscoveryService.h"
#include "GlobalHotkey.h"
#include "Settings.h"

#include <QFileSystemWatcher>
#include <QObject>
#include <QSystemTrayIcon>
#include <QTimer>

#include <memory>

class LauncherWindow;
class QAction;
class QMenu;

// Wires the components together: tray icon, global hotkey, discovery and the launcher window.
class NexoraApp : public QObject {
    Q_OBJECT

public:
    NexoraApp();
    ~NexoraApp() override;

    void start(bool showLauncher);

private:
    void buildTrayMenu();
    void applyHotkey(const QKeySequence& sequence, bool notifyOnFailure);
    void changeShortcut();
    void refresh();
    void refreshWithIcons();
    void refreshIfStale();
    void addApplication(const QString& path);
    void addFolder(const QString& path);
    void hideApplication(const QString& id);
    void restoreHiddenApplications();
    void updateWatchedFolders();
    AppDiscovery::Options discoveryOptions() const;

    Settings m_settings;
    DiscoveryService m_discovery;
    GlobalHotkey m_hotkey;
    std::unique_ptr<LauncherWindow> m_window;
    std::unique_ptr<QMenu> m_trayMenu;
    QSystemTrayIcon m_tray;
    QFileSystemWatcher m_watcher;     // Start menu and custom folders
    QTimer m_changeDebounce;          // coalesces bursts of changes from installers
    QAction* m_openAction = nullptr;
    QAction* m_restoreHiddenAction = nullptr;
};
