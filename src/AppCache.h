#pragma once

#include "AppEntry.h"

#include <vector>

// Persists discovered applications (including icons) so the launcher can show
// results instantly on start-up while a fresh scan runs in the background.
namespace AppCache {

std::vector<AppEntry> load();
bool save(const std::vector<AppEntry>& apps);

} // namespace AppCache
