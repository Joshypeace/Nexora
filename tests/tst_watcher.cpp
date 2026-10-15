// Folder watching: recursive detection, noise filtering, ignored paths, no locking.

#include "FolderWatcher.h"

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

class WatcherTest : public QObject {
    Q_OBJECT

private:
    static void write(const QString& path, const QByteArray& contents = "x")
    {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Append));
        file.write(contents);
    }

    // Lets pending notifications arrive, then reports whether any change was signalled.
    static bool settle(QSignalSpy& spy, int ms = 800)
    {
        QTest::qWait(ms);
        const bool any = !spy.isEmpty();
        spy.clear();
        return any;
    }

    QTemporaryDir m_root;
    std::unique_ptr<FolderWatcher> m_watcher;

private slots:
    void initTestCase()
    {
        QVERIFY(m_root.isValid());
        QDir(m_root.path()).mkpath(QStringLiteral("Tools/Editor"));
        QDir(m_root.path()).mkpath(QStringLiteral("data"));
        m_watcher = std::make_unique<FolderWatcher>();
        m_watcher->setFolders({m_root.path()}, {m_root.filePath(QStringLiteral("data"))});
    }

    void classifiesChanges()
    {
        QVERIFY(FolderWatcher::isRelevant(QStringLiteral("C:/x/App.exe"), true));
        QVERIFY(FolderWatcher::isRelevant(QStringLiteral("C:/x/App.lnk"), false));
        QVERIFY(FolderWatcher::isRelevant(QStringLiteral("C:/x/Game.url"), true));
        QVERIFY(!FolderWatcher::isRelevant(QStringLiteral("C:/x/log.txt"), true));
        QVERIFY(!FolderWatcher::isRelevant(QDir::tempPath() + QStringLiteral("/nexora-exists-check.dll"), true));
        // Removed paths can't be inspected, so they count.
        QVERIFY(FolderWatcher::isRelevant(QStringLiteral("C:/does/not/exist.dat"), false));
    }

    void detectsAppsDeepInTheTree()
    {
        QSignalSpy spy(m_watcher.get(), &FolderWatcher::changed);
        write(m_root.filePath(QStringLiteral("Tools/Editor/Editor.exe")));
        QVERIFY(spy.wait(3000));
        settle(spy);
    }

    void ignoresUnrelatedWrites()
    {
        write(m_root.filePath(QStringLiteral("Tools/Editor/settings.json")));
        QSignalSpy spy(m_watcher.get(), &FolderWatcher::changed);
        // Content writes to non-application files must not trigger rescans.
        write(m_root.filePath(QStringLiteral("Tools/Editor/settings.json")), "more");
        write(m_root.filePath(QStringLiteral("Tools/Editor/settings.json")), "and more");
        QVERIFY(!settle(spy));
    }

    void ignoresItsOwnDataFolder()
    {
        QSignalSpy spy(m_watcher.get(), &FolderWatcher::changed);
        write(m_root.filePath(QStringLiteral("data/apps.cache")));
        write(m_root.filePath(QStringLiteral("data/fake.exe")));
        QVERIFY(!settle(spy));
    }

    void neverLocksSubfolders()
    {
        QSignalSpy spy(m_watcher.get(), &FolderWatcher::changed);
        QDir root(m_root.path());
        QVERIFY2(root.rename(QStringLiteral("Tools"), QStringLiteral("Tools Renamed")), "subfolder rename was blocked");
        QVERIFY(spy.wait(3000)); // a folder rename can move applications
        QVERIFY(QDir(m_root.filePath(QStringLiteral("Tools Renamed"))).removeRecursively());
        settle(spy);
    }

    void stopsWatchingWhenCleared()
    {
        m_watcher->setFolders({}, {});
        QSignalSpy spy(m_watcher.get(), &FolderWatcher::changed);
        write(m_root.filePath(QStringLiteral("Later.exe")));
        QVERIFY(!settle(spy));
    }
};

QTEST_MAIN(WatcherTest)
#include "tst_watcher.moc"
