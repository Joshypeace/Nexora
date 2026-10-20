// Launch service behaviour that can be verified without starting real applications.

#include "AppLauncher.h"

#include <QSignalSpy>
#include <QtTest>

#include <optional>

class LauncherServiceTest : public QObject {
    Q_OBJECT

private:
    static AppEntry entry(AppKind kind, const QString& target)
    {
        AppEntry app;
        app.kind = kind;
        app.target = target;
        app.name = QStringLiteral("Test");
        return app;
    }

private slots:
    void elevationApplicability()
    {
        QVERIFY(AppLauncher::canElevate(entry(AppKind::Executable, QStringLiteral("C:\\a.exe"))));
        QVERIFY(AppLauncher::canElevate(entry(AppKind::Shortcut, QStringLiteral("C:\\a.lnk"))));
        QVERIFY(!AppLauncher::canElevate(entry(AppKind::Shortcut, QStringLiteral("C:\\Game.url"))));
        QVERIFY(!AppLauncher::canElevate(entry(AppKind::Shortcut, QStringLiteral("C:\\Click.appref-ms"))));
        QVERIFY(!AppLauncher::canElevate(entry(AppKind::Packaged, QStringLiteral("Pkg_abc!App"))));
    }

    void reportsMissingPrograms()
    {
        const AppLauncher::Result result =
            AppLauncher::launch(entry(AppKind::Executable, QStringLiteral("C:\\Omnipane\\does-not-exist.exe")));
        QVERIFY(!result.ok);
        QVERIFY(!result.error.isEmpty());
    }

    void asyncLaunchReportsOnTheCallerThread()
    {
        QObject context;
        std::optional<AppLauncher::Result> received;
        Qt::HANDLE callbackThread = nullptr;
        AppLauncher::launchAsync(entry(AppKind::Executable, QStringLiteral("C:\\Omnipane\\does-not-exist.exe")), false,
                                 &context, [&](const AppLauncher::Result& result) {
                                     received = result;
                                     callbackThread = QThread::currentThreadId();
                                 });
        QTRY_VERIFY_WITH_TIMEOUT(received.has_value(), 10000);
        QVERIFY(!received->ok);
        QCOMPARE(callbackThread, QThread::currentThreadId());
    }

    void asyncLaunchSkipsDestroyedContext()
    {
        bool called = false;
        {
            QObject context;
            AppLauncher::launchAsync(entry(AppKind::Executable, QStringLiteral("C:\\Omnipane\\does-not-exist.exe")),
                                     false, &context, [&](const AppLauncher::Result&) { called = true; });
        }
        QThreadPool::globalInstance()->waitForDone(10000);
        QCoreApplication::processEvents();
        QVERIFY(!called);
    }
};

QTEST_MAIN(LauncherServiceTest)
#include "tst_launcher.moc"
