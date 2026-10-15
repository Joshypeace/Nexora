#include "GlobalHotkey.h"
#include "NexoraApp.h"
#include "Theme.h"

#include <QApplication>
#include <QIcon>

#include <windows.h>

#include <memory>

namespace {

struct HandleCloser {
    void operator()(HANDLE handle) const { CloseHandle(handle); }
};
using UniqueHandle = std::unique_ptr<std::remove_pointer_t<HANDLE>, HandleCloser>;

} // namespace

int main(int argc, char* argv[])
{
    // One launcher per user session; a second start just opens the running one.
    const UniqueHandle instanceMutex(CreateMutexW(nullptr, FALSE, L"Local\\Nexora.SingleInstance"));
    if (instanceMutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        for (int attempt = 0; attempt < 30 && !GlobalHotkey::signalRunningInstance(); ++attempt)
            Sleep(100);
        return 0;
    }

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Nexora"));
    QApplication::setApplicationVersion(QStringLiteral(NEXORA_VERSION));
    QApplication::setQuitOnLastWindowClosed(false);
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/logo.png")));
    Theme::apply(app);

    const bool startHidden = QApplication::arguments().contains(QStringLiteral("--background"));

    NexoraApp nexora;
    nexora.start(!startHidden);
    return QApplication::exec();
}
