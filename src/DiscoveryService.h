#pragma once

#include "AppDiscovery.h"
#include "AppEntry.h"

#include <QDateTime>
#include <QHash>
#include <QObject>

#include <thread>
#include <vector>

// Runs application discovery and icon extraction on a background thread,
// keeps the current application list and persists it to the cache.
class DiscoveryService : public QObject {
    Q_OBJECT

public:
    explicit DiscoveryService(QObject* parent = nullptr);
    ~DiscoveryService() override;

    void loadCache();
    // Rescans in the background. Unchanged icons are reused unless `reloadIcons` is set.
    void refresh(const AppDiscovery::Options& options, bool reloadIcons = false);

    const std::vector<AppEntry>& apps() const { return m_apps; }
    bool isBusy() const { return m_busy; }
    QDateTime lastRefresh() const { return m_lastRefresh; }

    static constexpr int kIconSize = 96;

signals:
    void appsChanged();
    void iconsLoaded(const QHash<QString, QImage>& icons);
    void busyChanged(bool busy);

private:
    void run(std::stop_token stop, AppDiscovery::Options options, QHash<QString, AppEntry> cached, bool reloadIcons);
    void applyEntries(std::vector<AppEntry> entries);
    void applyIcons(const QHash<QString, QImage>& icons);
    void finish();
    void setBusy(bool busy);

    std::vector<AppEntry> m_apps;
    std::jthread m_worker;
    bool m_busy = false;
    bool m_refreshQueued = false;
    bool m_queuedReloadIcons = false;
    AppDiscovery::Options m_queuedOptions;
    QDateTime m_lastRefresh;
};
