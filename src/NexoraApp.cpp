#include "NexoraApp.h"

#include "LauncherWindow.h"
#include "ShortcutDialog.h"

#include <QApplication>
#include <QDir>
#include <QMenu>
#include <QMessageBox>

#include <windows.h>

namespace {

constexpr qint64 kStaleAfterSeconds = 10 * 60;

} // namespace

NexoraApp::NexoraApp()
    : m_window(std::make_unique<LauncherWindow>())
    , m_trayMenu(std::make_unique<QMenu>())
{
    m_window->setDimDesktop(m_settings.dimDesktop());
    m_window->setHiddenIds(m_settings.hiddenApps());

    connect(&m_discovery, &DiscoveryService::appsChanged, this, [this] {
        m_window->setApps(m_discovery.apps());
        QStringList paths;
        for (const AppEntry& app : m_discovery.apps()) {
            if (app.kind != AppKind::Packaged)
                paths << app.target << app.resolvedPath;
        }
        m_watcher.setKnownPaths(paths);
    });
    connect(&m_discovery, &DiscoveryService::iconsLoaded, m_window.get(), &LauncherWindow::updateIcons);
    connect(&m_discovery, &DiscoveryService::busyChanged, m_window.get(), &LauncherWindow::setBusy);

    connect(m_window.get(), &LauncherWindow::shown, this, &NexoraApp::refreshIfStale);
    connect(m_window.get(), &LauncherWindow::refreshRequested, this, &NexoraApp::refreshWithIcons);
    connect(m_window.get(), &LauncherWindow::addApplicationRequested, this, &NexoraApp::addApplication);
    connect(m_window.get(), &LauncherWindow::addFolderRequested, this, &NexoraApp::addFolder);
    connect(m_window.get(), &LauncherWindow::hideRequested, this, &NexoraApp::hideApplication);
    connect(m_window.get(), &LauncherWindow::launchFailed, this, [this](const QString& name, const QString& error) {
        m_tray.showMessage(QStringLiteral("Nexora"), tr("Couldn't open %1. %2").arg(name, error),
                           QSystemTrayIcon::Warning);
    });

    connect(&m_hotkey, &GlobalHotkey::activated, m_window.get(), &LauncherWindow::toggle);
    connect(&m_hotkey, &GlobalHotkey::activationRequested, m_window.get(), &LauncherWindow::summon);

    // Installers add Start menu entries in bursts; rescan once things settle.
    m_changeDebounce.setSingleShot(true);
    m_changeDebounce.setInterval(2500);
    connect(&m_changeDebounce, &QTimer::timeout, this, &NexoraApp::refresh);
    connect(&m_watcher, &FolderWatcher::changed, &m_changeDebounce, qOverload<>(&QTimer::start));
    connect(&m_discovery, &DiscoveryService::busyChanged, this, [this](bool busy) {
        // Release the memory a scan used while Nexora idles in the tray.
        if (!busy && !m_window->isVisible())
            SetProcessWorkingSetSize(GetCurrentProcess(), static_cast<SIZE_T>(-1), static_cast<SIZE_T>(-1));
    });

    buildTrayMenu();
    m_tray.setIcon(QApplication::windowIcon());
    m_tray.setToolTip(QStringLiteral("Nexora"));
    m_tray.setContextMenu(m_trayMenu.get());
    connect(&m_tray, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger)
            m_window->toggleFromTray();
    });
}

NexoraApp::~NexoraApp() = default;

void NexoraApp::start(bool showLauncher)
{
    applyHotkey(QKeySequence::fromString(m_settings.hotkey(), QKeySequence::PortableText), true);
    m_tray.show();

    m_discovery.loadCache();
    m_discovery.refresh(discoveryOptions());
    updateWatchedFolders();

    if (showLauncher)
        m_window->summon();
    else
        m_window->warmUp();
}

void NexoraApp::buildTrayMenu()
{
    QMenu* menu = m_trayMenu.get();
    m_openAction = menu->addAction(tr("Open Nexora"), m_window.get(), &LauncherWindow::summon);
    menu->addSeparator();
    menu->addAction(tr("Add application…"), this, [this] {
        m_window->summon();
        m_window->browseForApplication();
    });
    menu->addAction(tr("Add folder of apps…"), this, [this] {
        m_window->summon();
        m_window->browseForFolder();
    });
    menu->addAction(tr("Refresh applications"), this, &NexoraApp::refreshWithIcons);
    m_restoreHiddenAction = menu->addAction(tr("Show hidden applications"), this, &NexoraApp::restoreHiddenApplications);
    m_restoreHiddenAction->setEnabled(!m_settings.hiddenApps().isEmpty());
    menu->addSeparator();

    menu->addAction(tr("Change shortcut…"), this, &NexoraApp::changeShortcut);

    QAction* dim = menu->addAction(tr("Dim desktop behind launcher"));
    dim->setCheckable(true);
    dim->setChecked(m_settings.dimDesktop());
    connect(dim, &QAction::toggled, this, [this](bool enabled) {
        m_settings.setDimDesktop(enabled);
        m_window->setDimDesktop(enabled);
    });

    QAction* autostart = menu->addAction(tr("Start with Windows"));
    autostart->setCheckable(true);
    autostart->setChecked(Settings::launchAtStartup());
    connect(autostart, &QAction::toggled, this, [](bool enabled) { Settings::setLaunchAtStartup(enabled); });

    menu->addSeparator();
    menu->addAction(tr("Quit Nexora"), qApp, &QApplication::quit);
}

void NexoraApp::applyHotkey(const QKeySequence& sequence, bool notifyOnFailure)
{
    const bool ok = m_hotkey.setShortcut(sequence);
    const QString text = sequence.toString(QKeySequence::NativeText);
    m_openAction->setText(ok ? tr("Open Nexora\t%1").arg(text) : tr("Open Nexora"));
    if (!ok && notifyOnFailure) {
        m_tray.showMessage(QStringLiteral("Nexora"),
                           tr("The shortcut %1 is unavailable. Choose another one from the tray menu.").arg(text),
                           QSystemTrayIcon::Warning);
    }
}

void NexoraApp::changeShortcut()
{
    m_window->dismiss();
    const QKeySequence previous = m_hotkey.shortcut();

    // Release the current hotkey so the dialog can capture it (e.g. to re-enter Ctrl+Space).
    m_hotkey.setShortcut({});
    ShortcutDialog dialog(previous);
    if (dialog.exec() != QDialog::Accepted) {
        applyHotkey(previous, false);
        return;
    }

    const QKeySequence requested = dialog.sequence();
    if (m_hotkey.setShortcut(requested)) {
        m_settings.setHotkey(requested.toString(QKeySequence::PortableText));
        applyHotkey(requested, false);
        return;
    }

    QMessageBox::warning(nullptr, QStringLiteral("Nexora"),
                         tr("%1 can't be used: it is invalid or already taken by another application.")
                             .arg(requested.toString(QKeySequence::NativeText)));
    applyHotkey(previous, false);
}

void NexoraApp::updateWatchedFolders()
{
    // Our own cache and settings writes must never trigger a rescan (they would loop),
    // even if the data folder sits inside a watched custom folder.
    std::vector<FolderWatcher::Root> roots;
    for (const QString& folder : AppDiscovery::startMenuFolders())
        roots.push_back({folder, -1}); // the Start menu scan is unlimited in depth
    // Files at most kCustomFolderDepth subfolders down are scanned: that is depth + 1 path segments.
    for (const QString& folder : m_settings.customFolders())
        roots.push_back({folder, AppDiscovery::kCustomFolderDepth + 1});
    m_watcher.setFolders(roots, {Settings::dataDirectory()});
}

AppDiscovery::Options NexoraApp::discoveryOptions() const
{
    return {m_settings.customFolders(), m_settings.customApps()};
}

void NexoraApp::refresh()
{
    m_discovery.refresh(discoveryOptions());
}

void NexoraApp::refreshWithIcons()
{
    m_discovery.refresh(discoveryOptions(), true);
}

void NexoraApp::refreshIfStale()
{
    const QDateTime last = m_discovery.lastRefresh();
    if (!m_discovery.isBusy() && (!last.isValid() || last.secsTo(QDateTime::currentDateTimeUtc()) > kStaleAfterSeconds))
        refresh();
}

void NexoraApp::addApplication(const QString& path)
{
    m_settings.addCustomApp(QDir::toNativeSeparators(path));
    refresh();
}

void NexoraApp::addFolder(const QString& path)
{
    m_settings.addCustomFolder(QDir::toNativeSeparators(path));
    updateWatchedFolders();
    refresh();
}

void NexoraApp::hideApplication(const QString& id)
{
    m_settings.hideApp(id);
    m_window->setHiddenIds(m_settings.hiddenApps());
    m_restoreHiddenAction->setEnabled(true);
}

void NexoraApp::restoreHiddenApplications()
{
    m_settings.clearHiddenApps();
    m_window->setHiddenIds({});
    m_restoreHiddenAction->setEnabled(false);
}
