#pragma once

#include "AppEntry.h"

#include <QStringList>

#include <vector>

// Synchronous discovery primitives. They must run on a thread that has COM
// initialised (see DiscoveryService) and never execute any discovered program.
namespace AppDiscovery {

struct Options {
    QStringList customFolders;
    QStringList customApps;
};

// Returns deduplicated entries sorted by name. Icons are not loaded.
std::vector<AppEntry> discover(const Options& options);

// Builds an entry for a single user-selected .lnk or executable file.
bool entryForFile(const QString& path, AppSource source, AppEntry& out);

// Extracts the shell icon for `iconKey` as a premultiplied ARGB image of `size` pixels.
QImage extractIcon(const QString& iconKey, int size);

} // namespace AppDiscovery
