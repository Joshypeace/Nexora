// Global shortcut validation and registration.

#include "GlobalHotkey.h"

#include <QtTest>

class HotkeyTest : public QObject {
    Q_OBJECT

private slots:
    void acceptance_data()
    {
        QTest::addColumn<QString>("sequence");
        QTest::addColumn<bool>("acceptable");

        QTest::newRow("ctrl+space") << "Ctrl+Space" << true;
        QTest::newRow("alt+space") << "Alt+Space" << true;
        QTest::newRow("win+k") << "Meta+K" << true;
        QTest::newRow("ctrl+shift+f5") << "Ctrl+Shift+F5" << true;
        QTest::newRow("ctrl+numpad") << "Ctrl+Num+5" << true;
        QTest::newRow("bare f13") << "F13" << true;
        QTest::newRow("bare f24") << "F24" << true;
        QTest::newRow("bare space") << "Space" << false;
        QTest::newRow("bare letter") << "A" << false;
        QTest::newRow("shift+letter") << "Shift+A" << false;
        QTest::newRow("bare f1") << "F1" << false;
        QTest::newRow("empty") << "" << false;
        QTest::newRow("two chords") << "Ctrl+A, Ctrl+B" << false;
    }

    void acceptance()
    {
        QFETCH(QString, sequence);
        QFETCH(bool, acceptable);
        QCOMPARE(GlobalHotkey::isAcceptable(QKeySequence::fromString(sequence, QKeySequence::PortableText)),
                 acceptable);
    }

    void registersAndReleases()
    {
        // An obscure combination that no other application is likely to own.
        const QKeySequence sequence = QKeySequence::fromString(QStringLiteral("Ctrl+Alt+Shift+F23"));
        GlobalHotkey hotkey;
        QVERIFY(hotkey.setShortcut(sequence));
        QCOMPARE(hotkey.shortcut(), sequence);

        // The same combination cannot be registered twice.
        GlobalHotkey second;
        QVERIFY(!second.setShortcut(sequence));
        QVERIFY(second.shortcut().isEmpty());

        // Clearing releases it for others.
        QVERIFY(!hotkey.setShortcut({}));
        QVERIFY(second.setShortcut(sequence));
    }

    void refusesUnsafeShortcuts()
    {
        GlobalHotkey hotkey;
        QVERIFY(!hotkey.setShortcut(QKeySequence(Qt::Key_Space)));
        QVERIFY(hotkey.shortcut().isEmpty());
    }
};

QTEST_MAIN(HotkeyTest)
#include "tst_hotkey.moc"
