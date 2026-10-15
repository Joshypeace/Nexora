#include "GlobalHotkey.h"
#include "NexoraApp.h"

#include <QApplication>
#include <QFont>
#include <QIcon>
#include <QPalette>
#include <QStyleFactory>

#include <windows.h>

#include <memory>

namespace {

struct HandleCloser {
    void operator()(HANDLE handle) const { CloseHandle(handle); }
};
using UniqueHandle = std::unique_ptr<std::remove_pointer_t<HANDLE>, HandleCloser>;

// Styling shared by every Nexora window, menu and dialog.
void applyTheme(QApplication& app)
{
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    app.setFont(QFont(QStringList{QStringLiteral("Segoe UI Variable Text"), QStringLiteral("Segoe UI")}, 10));

    QPalette palette;
    palette.setColor(QPalette::Window, QColor(18, 20, 36));
    palette.setColor(QPalette::WindowText, QColor(230, 233, 245));
    palette.setColor(QPalette::Base, QColor(26, 29, 50));
    palette.setColor(QPalette::AlternateBase, QColor(30, 33, 56));
    palette.setColor(QPalette::Text, QColor(230, 233, 245));
    palette.setColor(QPalette::Button, QColor(32, 36, 60));
    palette.setColor(QPalette::ButtonText, QColor(230, 233, 245));
    palette.setColor(QPalette::Highlight, QColor(124, 92, 246));
    palette.setColor(QPalette::HighlightedText, Qt::white);
    palette.setColor(QPalette::ToolTipBase, QColor(22, 24, 42));
    palette.setColor(QPalette::ToolTipText, QColor(230, 233, 245));
    palette.setColor(QPalette::PlaceholderText, QColor(170, 178, 210, 150));
    app.setPalette(palette);

    app.setStyleSheet(QStringLiteral(R"(
QMenu {
    background: #15182A;
    border: 1px solid #2A2F4C;
    padding: 6px;
    color: #E6E9F5;
}
QMenu::item { padding: 7px 26px 7px 14px; border-radius: 6px; }
QMenu::item:selected { background: rgba(124, 92, 246, 110); }
QMenu::item:disabled { color: #6B7194; }
QMenu::separator { height: 1px; background: #262A45; margin: 5px 8px; }
QMenu::indicator { width: 14px; height: 14px; left: 6px; }
QToolTip { background: #16182A; color: #E6E9F5; border: 1px solid #2A2F4C; padding: 4px 8px; }
)"));
}

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
    applyTheme(app);

    const bool startHidden = QApplication::arguments().contains(QStringLiteral("--background"));

    NexoraApp nexora;
    nexora.start(!startHidden);
    return QApplication::exec();
}
