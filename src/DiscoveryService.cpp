#include "DiscoveryService.h"

#include "AppCache.h"
#include "ComApartment.h"

#include <QMetaObject>

namespace {

constexpr int kIconBatchSize = 24;

} // namespace

DiscoveryService::DiscoveryService(QObject* parent)
    : QObject(parent)
{
}

DiscoveryService::~DiscoveryService()
{
    // Stop and join the worker before members are destroyed; queued callbacks to
    // this object are discarded by Qt once it is gone.
    if (m_worker.joinable()) {
        m_worker.request_stop();
        m_worker.join();
    }
}

void DiscoveryService::loadCache()
{
    m_apps = AppCache::load();
    emit appsChanged();
}

void DiscoveryService::refresh(const AppDiscovery::Options& options, bool reloadIcons)
{
    if (m_busy) {
        m_refreshQueued = true;
        m_queuedReloadIcons = m_queuedReloadIcons || reloadIcons;
        m_queuedOptions = options;
        return;
    }

    if (m_worker.joinable())
        m_worker.join();

    QHash<QString, AppEntry> cached;
    for (const AppEntry& app : m_apps)
        cached.insert(app.id, app);

    setBusy(true);
    m_worker = std::jthread([this, options, cached = std::move(cached), reloadIcons](std::stop_token stop) mutable {
        run(stop, std::move(options), std::move(cached), reloadIcons);
    });
}

void DiscoveryService::run(std::stop_token stop, AppDiscovery::Options options, QHash<QString, AppEntry> cached,
                           bool reloadIcons)
{
    const ComApartment com;

    std::vector<AppEntry> entries = AppDiscovery::discover(options, stop);

    // Reuse cached icons whose source has not changed. When reloading, the cached icon is
    // still shown until its replacement arrives, so the grid never blanks out.
    std::vector<size_t> missing;
    for (size_t i = 0; i < entries.size(); ++i) {
        AppEntry& entry = entries[i];
        const auto it = cached.constFind(entry.id);
        const bool hasCached = it != cached.constEnd() && !it->icon.isNull();
        if (hasCached)
            entry.icon = it->icon;
        if (!hasCached || reloadIcons || it->iconKey != entry.iconKey || it->iconStamp != entry.iconStamp)
            missing.push_back(i);
    }

    if (stop.stop_requested())
        return;
    // Qt containers are implicitly shared, so handing the UI a copy is cheap.
    QMetaObject::invokeMethod(this, [this, copy = entries]() mutable { applyEntries(std::move(copy)); },
                              Qt::QueuedConnection);

    QHash<QString, QImage> batch;
    for (const size_t index : missing) {
        if (stop.stop_requested())
            return;
        AppEntry& entry = entries[index];
        const QImage icon = AppDiscovery::extractIcon(entry.iconKey, kIconSize);
        if (icon.isNull() && !entry.icon.isNull())
            continue; // keep the previous icon rather than blanking it
        entry.icon = icon;
        batch.insert(entry.id, entry.icon);
        if (batch.size() >= kIconBatchSize) {
            QMetaObject::invokeMethod(this, [this, batch] { applyIcons(batch); }, Qt::QueuedConnection);
            batch.clear();
        }
    }

    AppCache::save(entries);
    QMetaObject::invokeMethod(this, [this, batch] { applyIcons(batch); finish(); }, Qt::QueuedConnection);
}

void DiscoveryService::applyEntries(std::vector<AppEntry> entries)
{
    m_apps = std::move(entries);
    emit appsChanged();
}

void DiscoveryService::applyIcons(const QHash<QString, QImage>& icons)
{
    if (icons.isEmpty())
        return;
    for (AppEntry& app : m_apps) {
        const auto it = icons.constFind(app.id);
        if (it != icons.constEnd())
            app.icon = *it;
    }
    emit iconsLoaded(icons);
}

void DiscoveryService::finish()
{
    m_lastRefresh = QDateTime::currentDateTimeUtc();
    setBusy(false);

    if (m_refreshQueued) {
        const bool reloadIcons = m_queuedReloadIcons;
        m_refreshQueued = false;
        m_queuedReloadIcons = false;
        refresh(m_queuedOptions, reloadIcons);
    }
}

void DiscoveryService::setBusy(bool busy)
{
    if (m_busy == busy)
        return;
    m_busy = busy;
    emit busyChanged(busy);
}
