#pragma once

#include "AppEntry.h"

#include <QString>

#include <functional>

class QObject;

// Starts discovered applications through the Windows shell.
namespace AppLauncher {

struct Result {
    bool ok = false;
    QString error;
};

// Blocking launch; requires COM to be initialised on the calling thread.
Result launch(const AppEntry& app, bool elevated = false);

// Launches on a worker thread so slow starts (packaged app activation, UAC,
// network paths) never freeze the UI. `onFinished` runs on `context`'s thread.
void launchAsync(const AppEntry& app, bool elevated, QObject* context, std::function<void(const Result&)> onFinished);

// Opens Explorer with the application's file selected. Not available for packaged apps.
bool revealInExplorer(const AppEntry& app);

} // namespace AppLauncher
