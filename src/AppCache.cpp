#include "AppCache.h"

#include "Settings.h"

#include <QDataStream>
#include <QFile>
#include <QSaveFile>

namespace {

constexpr quint32 kMagic = 0x4E58524Fu; // "NXRO"
constexpr quint32 kVersion = 2;

QString cachePath()
{
    return Settings::dataDirectory() + QStringLiteral("/apps.cache");
}

} // namespace

namespace AppCache {

std::vector<AppEntry> load()
{
    QFile file(cachePath());
    if (!file.open(QIODevice::ReadOnly))
        return {};

    QDataStream in(&file);
    in.setVersion(QDataStream::Qt_6_5);

    quint32 magic = 0;
    quint32 version = 0;
    quint32 count = 0;
    in >> magic >> version >> count;
    if (magic != kMagic || version != kVersion || count > 100000)
        return {};

    std::vector<AppEntry> apps;
    apps.reserve(count);
    for (quint32 i = 0; i < count && in.status() == QDataStream::Ok; ++i) {
        AppEntry app;
        quint8 kind = 0;
        quint8 source = 0;
        in >> app.id >> app.name >> kind >> source >> app.target >> app.resolvedPath
           >> app.iconKey >> app.iconStamp >> app.icon;
        app.kind = static_cast<AppKind>(kind);
        app.source = static_cast<AppSource>(source);
        apps.push_back(std::move(app));
    }

    if (in.status() != QDataStream::Ok)
        return {};
    return apps;
}

bool save(const std::vector<AppEntry>& apps)
{
    QSaveFile file(cachePath());
    if (!file.open(QIODevice::WriteOnly))
        return false;

    QDataStream out(&file);
    out.setVersion(QDataStream::Qt_6_5);
    out << kMagic << kVersion << static_cast<quint32>(apps.size());
    for (const AppEntry& app : apps) {
        out << app.id << app.name << static_cast<quint8>(app.kind) << static_cast<quint8>(app.source)
            << app.target << app.resolvedPath << app.iconKey << app.iconStamp << app.icon;
    }
    return out.status() == QDataStream::Ok && file.commit();
}

} // namespace AppCache
