#include "AppLauncher.h"

#include "ComApartment.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QMetaObject>
#include <QPointer>
#include <QThreadPool>

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <wrl/client.h>

#include <memory>
#include <string>

using Microsoft::WRL::ComPtr;

namespace {

QString systemErrorMessage(DWORD code)
{
    wchar_t* raw = nullptr;
    const DWORD length = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM
                                            | FORMAT_MESSAGE_IGNORE_INSERTS,
                                        nullptr, code, 0, reinterpret_cast<LPWSTR>(&raw), 0, nullptr);
    const std::unique_ptr<wchar_t, decltype(&LocalFree)> buffer(raw, &LocalFree);
    if (length == 0)
        return QStringLiteral("Error %1").arg(code);
    return QString::fromWCharArray(raw, static_cast<qsizetype>(length)).trimmed();
}

AppLauncher::Result shellExecute(const QString& file, const QString& directory, bool elevated)
{
    const std::wstring nativeFile = QDir::toNativeSeparators(file).toStdWString();
    const std::wstring nativeDirectory = QDir::toNativeSeparators(directory).toStdWString();

    SHELLEXECUTEINFOW info = {};
    info.cbSize = sizeof(info);
    // NO_UI: failures are reported once, through Nexora's tray notification. UAC still prompts.
    info.fMask = SEE_MASK_NOASYNC | SEE_MASK_FLAG_LOG_USAGE | SEE_MASK_FLAG_NO_UI;
    info.lpVerb = elevated ? L"runas" : nullptr;
    info.lpFile = nativeFile.c_str();
    info.lpDirectory = nativeDirectory.empty() ? nullptr : nativeDirectory.c_str();
    info.nShow = SW_SHOWNORMAL;

    if (ShellExecuteExW(&info))
        return {true, {}};

    const DWORD error = GetLastError();
    if (error == ERROR_CANCELLED) // The user declined the UAC prompt.
        return {true, {}};
    return {false, systemErrorMessage(error)};
}

AppLauncher::Result activatePackagedApp(const QString& aumid)
{
    ComPtr<IApplicationActivationManager> manager;
    HRESULT hr = CoCreateInstance(CLSID_ApplicationActivationManager, nullptr, CLSCTX_LOCAL_SERVER,
                                  IID_PPV_ARGS(&manager));
    if (SUCCEEDED(hr)) {
        const std::wstring id = aumid.toStdWString();
        DWORD processId = 0;
        hr = manager->ActivateApplication(id.c_str(), nullptr, AO_NONE, &processId);
        if (SUCCEEDED(hr))
            return {true, {}};
    }

    // Fall back to the shell's Apps folder, which also covers apps the activation manager rejects.
    return shellExecute(QStringLiteral("shell:AppsFolder\\") + aumid, {}, false);
}

} // namespace

namespace AppLauncher {

bool canElevate(const AppEntry& app)
{
    if (app.kind == AppKind::Packaged)
        return false;
    const QString suffix = QFileInfo(app.target).suffix().toLower();
    return suffix != u"url" && suffix != u"appref-ms";
}

Result launch(const AppEntry& app, bool elevated)
{
    elevated = elevated && canElevate(app);
    switch (app.kind) {
    case AppKind::Packaged:
        return activatePackagedApp(app.target);
    case AppKind::Shortcut:
        // Launching the .lnk itself preserves its arguments, working directory and run-as settings.
        return shellExecute(app.target, {}, elevated);
    case AppKind::Executable:
        return shellExecute(app.target, QFileInfo(app.target).absolutePath(), elevated);
    }
    return {false, QStringLiteral("Unknown application type")};
}

void launchAsync(const AppEntry& app, bool elevated, QObject* context, std::function<void(const Result&)> onFinished)
{
    // Nexora hides as soon as the launch starts; let the new application take the foreground.
    AllowSetForegroundWindow(ASFW_ANY);

    // QPointer may only be dereferenced on its object's thread, so the result is posted
    // to the application object and `context` is checked there.
    QThreadPool::globalInstance()->start([app, elevated, context = QPointer<QObject>(context),
                                          onFinished = std::move(onFinished)]() mutable {
        const ComApartment com;
        const Result result = launch(app, elevated);
        QMetaObject::invokeMethod(
            QCoreApplication::instance(),
            [context = std::move(context), onFinished = std::move(onFinished), result] {
                if (context)
                    onFinished(result);
            },
            Qt::QueuedConnection);
    });
}

bool revealInExplorer(const AppEntry& app)
{
    if (app.kind == AppKind::Packaged)
        return false;

    const QString path = !app.resolvedPath.isEmpty() && QFileInfo::exists(app.resolvedPath) ? app.resolvedPath
                                                                                            : app.target;
    const std::wstring native = QDir::toNativeSeparators(path).toStdWString();
    PIDLIST_ABSOLUTE item = ILCreateFromPathW(native.c_str());
    if (!item)
        return false;
    const bool ok = SUCCEEDED(SHOpenFolderAndSelectItems(item, 0, nullptr, 0));
    ILFree(item);
    return ok;
}

} // namespace AppLauncher
