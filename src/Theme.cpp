#include "Theme.h"

#include <QApplication>
#include <QEvent>
#include <QFont>
#include <QIcon>
#include <QMenu>
#include <QPalette>
#include <QStyleFactory>

namespace {

const char* const kStyleSheet = R"(
QMenu {
    background: rgba(20, 22, 40, 246);
    border: 1px solid rgba(255, 255, 255, 26);
    border-radius: 10px;
    padding: 6px;
    color: #E6E9F5;
}
QMenu::item { padding: 7px 26px 7px 14px; border-radius: 6px; background: transparent; }
QMenu::item:selected {
    background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
        stop:0 rgba(139, 92, 246, 120), stop:1 rgba(59, 130, 246, 100));
}
QMenu::item:disabled { color: #6B7194; }
QMenu::separator { height: 1px; background: rgba(255, 255, 255, 18); margin: 5px 10px; }
QMenu::indicator { width: 14px; height: 14px; left: 6px; }
QToolTip { background: #16182A; color: #E6E9F5; border: 1px solid #2A2F4C; padding: 4px 8px; }
)";

// Gives every menu (ours and Qt's built-in ones, e.g. the search field's) rounded,
// translucent corners. Runs on Polish, before the native popup window is created.
class MenuPolisher : public QObject {
public:
    using QObject::QObject;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (event->type() == QEvent::Polish) {
            auto* menu = qobject_cast<QMenu*>(watched);
            if (menu && !menu->testAttribute(Qt::WA_TranslucentBackground)) {
                menu->setAttribute(Qt::WA_TranslucentBackground);
                menu->setWindowFlag(Qt::FramelessWindowHint);
                menu->setWindowFlag(Qt::NoDropShadowWindowHint);
            }
        }
        return QObject::eventFilter(watched, event);
    }
};

} // namespace

namespace Theme {

QIcon appIcon()
{
    QIcon icon;
    for (const int size : {16, 20, 24, 32, 40, 48})
        icon.addFile(QStringLiteral(":/logo-%1.png").arg(size), QSize(size, size));
    icon.addFile(QStringLiteral(":/logo.png"), QSize(256, 256));
    return icon;
}

void apply(QApplication& app)
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

    app.setStyleSheet(QString::fromLatin1(kStyleSheet));
    app.installEventFilter(new MenuPolisher(&app));
}

} // namespace Theme
