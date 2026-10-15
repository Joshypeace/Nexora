#pragma once

#include "AppEntry.h"

#include <QStringList>

#include <stop_token>
#include <vector>

// Synchronous discovery primitives. They must run on a thread that has COM
// initialised (see DiscoveryService) and never execute any discovered program.
namespace AppDiscovery {

struct Options {
    QStringList customFolders;
    QStringList customApps;
};

// Returns deduplicated entries sorted by name. Icons are not loaded. Returns early
// (with partial results) once `stop` is requested.
std::vector<AppEntry> discover(const Options& options, const std::stop_token& stop);

// Custom folders are scanned this many subfolder levels deep (0 = only the folder itself).
constexpr int kCustomFolderDepth = 2;

// The user's and all users' Start menu "Programs" folders, for change watching.
QStringList startMenuFolders();

// Builds an entry for a single user-selected file (.lnk, .url, .appref-ms, .exe, ...).
bool entryForFile(const QString& path, AppSource source, AppEntry& out);

// Extracts the shell icon for `iconKey` as a premultiplied ARGB image of `size` pixels.
QImage extractIcon(const QString& iconKey, int size);

} // namespace AppDiscovery
