#pragma once

#include <QImage>
#include <QString>

// How an application is started.
enum class AppKind : quint8 {
    Shortcut,   // A .lnk file; the shell resolves target, arguments and working directory.
    Executable, // A file path started directly (exe, bat, msc, ...).
    Packaged,   // A packaged / Microsoft Store app identified by its AppUserModelID.
};

// Where an application was discovered. Lower values win during deduplication.
enum class AppSource : quint8 {
    Manual,
    CustomFolder,
    StartMenu,
    Packaged,
    Registry,
};

struct AppEntry {
    QString id;           // Stable identity used for deduplication, hiding and caching.
    QString name;         // Display name.
    AppKind kind = AppKind::Executable;
    AppSource source = AppSource::StartMenu;
    QString target;       // Shortcut path, executable path, or AUMID for packaged apps.
    QString resolvedPath; // Executable a shortcut points to; empty when unknown.
    QString iconKey;      // Shell parsing name used to extract the icon.
    qint64 iconStamp = 0; // Modification time of the icon source, for cache invalidation.
    QImage icon;
};
