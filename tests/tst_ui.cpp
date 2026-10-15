// End-to-end UI checks of the real launcher window using Qt's in-process event
// synthesis: typing, keyboard navigation, hover, the "+" menu, the item context
// menu, empty/loading states and dismissal. Snapshots are saved for visual review.
// Mouse events use the QWindow overloads, which never move the OS cursor.
// If the developer clicks elsewhere mid-run the launcher dismisses itself (by design);
// the remaining tests are then skipped rather than failed.

#include "AppLauncher.h"
#include "LauncherWindow.h"
#include "Settings.h"
#include "Theme.h"

#include <QAbstractButton>
#include <QApplication>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QPainter>
#include <QScreen>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>

class UiTest : public QObject {
    Q_OBJECT

private:
    static AppEntry app(const QString& name, const QColor& color)
    {
        AppEntry entry;
        entry.id = QStringLiteral("app:") + name.toLower();
        entry.name = name;
        entry.kind = AppKind::Executable;
        entry.target = QStringLiteral("C:\\Nexora-test\\") + name + QStringLiteral(".exe"); // never exists
        entry.resolvedPath = entry.target;

        QImage icon(96, 96, QImage::Format_ARGB32_Premultiplied);
        icon.fill(Qt::transparent);
        QPainter painter(&icon);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        painter.drawRoundedRect(QRectF(8, 8, 80, 80), 20, 20);
        entry.icon = icon;
        return entry;
    }

    // Saves what the widget renders, plus the composited on-screen pixels (which include
    // translucency, DWM effects and whatever is behind the window).
    void snapshot(QWidget* widget, const QString& name)
    {
        const QString dir = qEnvironmentVariable("NEXORA_SNAPSHOT_DIR", QDir::currentPath() + QStringLiteral("/snapshots"));
        QDir().mkpath(dir);
        QVERIFY(widget->grab().save(dir + u'/' + name + QStringLiteral(".png")));
        const QRect frame = widget->frameGeometry();
        const QPixmap screen = widget->screen()->grabWindow(0, frame.x(), frame.y(), frame.width(), frame.height());
        QVERIFY(screen.save(dir + u'/' + name + QStringLiteral("-onscreen.png")));
    }

    QMenu* waitForPopup()
    {
        QMenu* menu = nullptr;
        const bool appeared = QTest::qWaitFor(
            [&] { return (menu = qobject_cast<QMenu*>(QApplication::activePopupWidget())) != nullptr; }, 3000);
        if (!appeared)
            return nullptr;
        QTest::qWait(150); // let the popup finish painting before grabbing it
        return menu; // callers QVERIFY the result
    }

    static QStringList actionTexts(const QMenu* menu)
    {
        QStringList texts;
        for (const QAction* action : menu->actions()) {
            if (!action->isSeparator())
                texts.append(action->text());
        }
        return texts;
    }

    QModelIndex current() const { return m_view->currentIndex(); }

    QTemporaryDir m_dataDir;
    std::unique_ptr<LauncherWindow> m_window;
    QLineEdit* m_search = nullptr;
    QListView* m_view = nullptr;

private slots:
    void initTestCase()
    {
        QVERIFY(m_dataDir.isValid());
        qputenv("NEXORA_DATA_DIR", m_dataDir.path().toUtf8());
        Theme::apply(*qApp);

        m_window = std::make_unique<LauncherWindow>();
        m_window->setDimDesktop(false); // don't dim the developer's screens during tests

        const QStringList names = {"Blender", "Calculator", "Calendar", "Camera", "Character Map", "Chrome",
                                   "Clock", "Command Prompt", "Firefox", "Notepad", "Paint", "Photos",
                                   "Terminal", "Visual Studio Code"};
        std::vector<AppEntry> apps;
        for (qsizetype i = 0; i < names.size(); ++i)
            apps.push_back(app(names[i], QColor::fromHsv(int(i * 360 / names.size()), 160, 230)));
        m_window->setApps(apps);

        m_window->summon();
        QVERIFY(QTest::qWaitForWindowExposed(m_window.get()));
        QTest::qWait(300); // reveal animation

        m_search = m_window->findChild<QLineEdit*>(QStringLiteral("search"));
        m_view = m_window->findChild<QListView*>();
        QVERIFY(m_search);
        QVERIFY(m_view);
        snapshot(m_window.get(), QStringLiteral("01-launcher"));
    }

    void init()
    {
        if (qstrcmp(QTest::currentTestFunction(), "escapeDismisses") != 0 && !m_window->isVisible())
            QSKIP("the launcher was dismissed by an outside activation change (user input during the run?)");
    }

    void showsAllAppsSelectingTheFirst()
    {
        QCOMPARE(m_view->model()->rowCount(), 14);
        QCOMPARE(current().row(), 0);
        QCOMPARE(current().data().toString(), QStringLiteral("Blender"));
    }

    void filtersAsYouType()
    {
        QTest::keyClicks(m_search, QStringLiteral("cal"));
        QCOMPARE(m_view->model()->rowCount(), 2);
        QCOMPARE(current().data().toString(), QStringLiteral("Calculator"));
        snapshot(m_window.get(), QStringLiteral("02-search"));
        m_search->clear();
        QCOMPARE(m_view->model()->rowCount(), 14);
    }

    void navigatesWithTheKeyboard()
    {
        QCOMPARE(current().row(), 0);
        QTest::keyClick(m_search, Qt::Key_Right);
        QCOMPARE(current().row(), 1);
        QTest::keyClick(m_search, Qt::Key_Tab);
        QCOMPARE(current().row(), 2);
        QTest::keyClick(m_search, Qt::Key_Backtab);
        QCOMPARE(current().row(), 1);

        // 14 apps fit without a scroll bar; the grid must still use the full width (6 columns).
        const int columns = m_view->viewport()->width() / m_view->gridSize().width();
        QCOMPARE(columns, 6);
        QTest::keyClick(m_search, Qt::Key_Down);
        QCOMPARE(current().row(), 1 + columns);
        QTest::keyClick(m_search, Qt::Key_Up);
        QCOMPARE(current().row(), 1);
        QTest::keyClick(m_search, Qt::Key_Left);
        QCOMPARE(current().row(), 0);
    }

    void highlightsOnHover()
    {
        const QModelIndex target = m_view->model()->index(3, 0);
        const QRect rect = m_view->visualRect(target);
        const QImage before = m_view->viewport()->grab().toImage();
        // The QWindow overload injects through Qt's platform layer; the QWidget overload
        // would move the real OS cursor (QCursor::setPos).
        QTest::mouseMove(m_window->windowHandle(), m_view->viewport()->mapTo(m_window.get(), rect.center()));
        QTest::qWait(150);
        QCOMPARE(m_view->indexAt(rect.center()), target);
        const QImage after = m_view->viewport()->grab().toImage();
        // The hover highlight must change the tile's background (corner area, away from icon and text).
        const QPoint probe = ((QPointF(rect.topLeft()) + QPointF(14, 14)) * before.devicePixelRatio()).toPoint();
        qInfo() << "hover probe" << rect << probe << Qt::hex << before.pixel(probe) << after.pixel(probe)
                << "dpr" << before.devicePixelRatio();
        QVERIFY2(before.pixel(probe) != after.pixel(probe), "hovered tile was not highlighted");
        snapshot(m_window.get(), QStringLiteral("03-hover"));
    }

    void opensTheAddMenu()
    {
        QAbstractButton* add = nullptr;
        for (QAbstractButton* button : m_window->findChildren<QAbstractButton*>()) {
            if (button->toolTip() == QStringLiteral("Add applications"))
                add = button;
        }
        QVERIFY(add);
        QTest::mouseClick(m_window->windowHandle(), Qt::LeftButton, {},
                          add->mapTo(m_window.get(), add->rect().center()));
        QMenu* menu = waitForPopup();
        QVERIFY(menu);
        QCOMPARE(actionTexts(menu),
                 QStringList({"Add application…", "Add folder of apps…", "Refresh applications"}));
        snapshot(menu, QStringLiteral("04-add-menu"));
        menu->close();
        QTRY_VERIFY(!QApplication::activePopupWidget());
        QVERIFY(m_window->isVisible()); // a popup must not dismiss the launcher
    }

    void opensTheItemContextMenu()
    {
        const QModelIndex target = m_view->model()->index(2, 0);
        const QPoint position = m_view->visualRect(target).center();
        QContextMenuEvent event(QContextMenuEvent::Mouse, position, m_view->viewport()->mapToGlobal(position));
        QApplication::sendEvent(m_view->viewport(), &event);

        QMenu* menu = waitForPopup();
        QVERIFY(menu);
        QCOMPARE(current().row(), 2); // right-click selects the item
        QCOMPARE(actionTexts(menu),
                 QStringList({"Open", "Run as administrator", "Open file location", "Hide from Nexora"}));
        snapshot(menu, QStringLiteral("05-context-menu"));
        menu->close();
        QTRY_VERIFY(!QApplication::activePopupWidget());
    }

    void hidesAnAppOnRequest()
    {
        QSignalSpy hide(m_window.get(), &LauncherWindow::hideRequested);
        m_window->setHiddenIds({QStringLiteral("app:notepad")});
        QCOMPARE(m_view->model()->rowCount(), 13);
        m_window->setHiddenIds({});
        QCOMPARE(m_view->model()->rowCount(), 14);
        QCOMPARE(hide.count(), 0);
    }

    void showsNoMatchesState()
    {
        QTest::keyClicks(m_search, QStringLiteral("zzzz"));
        auto* title = m_window->findChild<QLabel*>(QStringLiteral("emptyTitle"));
        QVERIFY(title && title->isVisible());
        QVERIFY(title->text().startsWith(QStringLiteral("No matches")));
        snapshot(m_window.get(), QStringLiteral("06-no-matches"));
        m_search->clear();
    }

    void showsLoadingState()
    {
        const std::vector<AppEntry> none;
        m_window->setApps(none);
        m_window->setBusy(true);
        auto* title = m_window->findChild<QLabel*>(QStringLiteral("emptyTitle"));
        QVERIFY(title && title->isVisible());
        QCOMPARE(title->text(), QStringLiteral("Discovering applications…"));
        QTest::qWait(200);
        snapshot(m_window.get(), QStringLiteral("07-loading"));
        m_window->setBusy(false);
        QCOMPARE(title->text(), QStringLiteral("No applications found"));

        m_window->setApps({app(QStringLiteral("Missing App"), Qt::red)});
    }

    void enterLaunchesAndReportsFailure()
    {
        QSignalSpy failed(m_window.get(), &LauncherWindow::launchFailed);
        QTest::keyClick(m_search, Qt::Key_Return);
        QVERIFY(!m_window->isVisible()); // hides immediately, before the launch completes
        QVERIFY(failed.wait(10000));
        QCOMPARE(failed.first().at(0).toString(), QStringLiteral("Missing App"));
    }

    void escapeDismisses()
    {
        m_window->summon();
        QVERIFY(QTest::qWaitForWindowExposed(m_window.get()));
        QTest::keyClick(m_search, Qt::Key_Escape);
        QVERIFY(!m_window->isVisible());
    }

    void cleanupTestCase()
    {
        m_window.reset();
    }
};

QTEST_MAIN(UiTest)
#include "tst_ui.moc"
