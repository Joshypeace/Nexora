// Folder watching: recursive detection, noise filtering, depth limits, ignored paths,
// no locking, and recovery of roots that appear later or are recreated.

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
        // Same depth rule as a custom folder: files at most two subfolders down.
        m_watcher->setFolders({{m_root.path(), 3}}, {m_root.filePath(QStringLiteral("data"))});
        QCOMPARE(m_watcher->activeCount(), 1);
    }

    void classifiesChanges()
    {
        QVERIFY(FolderWatcher::isRelevant(QStringLiteral("C:/x/App.exe"), true));
        QVERIFY(FolderWatcher::isRelevant(QStringLiteral("C:/x/App.lnk"), false));
        QVERIFY(FolderWatcher::isRelevant(QStringLiteral("C:/x/Game.url"), true));
        QVERIFY(!FolderWatcher::isRelevant(QStringLiteral("C:/x/log.txt"), true));
        // Vanished paths count only if they could have been a folder or an app.
        QVERIFY(FolderWatcher::isRelevant(QStringLiteral("C:/does/not/exist"), false));
        QVERIFY(FolderWatcher::isRelevant(QStringLiteral("C:/does/not/Old.exe"), false));
        QVERIFY(!FolderWatcher::isRelevant(QStringLiteral("C:/does/not/exist.tmp"), false));
        // Existing extension-less files (browser caches) are not folders.
        const QString cacheFile = m_root.filePath(QStringLiteral("data/cachefile"));
        write(cacheFile);
        QVERIFY(!FolderWatcher::isRelevant(cacheFile, false));
        QVERIFY(FolderWatcher::isRelevant(m_root.path(), false)); // an existing folder
    }

    void detectsAppsWithinTheScannedDepth()
    {
        QSignalSpy spy(m_watcher.get(), &FolderWatcher::changed);
        write(m_root.filePath(QStringLiteral("Tools/Editor/Editor.exe")));
        QVERIFY(spy.wait(3000));
        settle(spy);
    }

    void ignoresChangesBelowTheScannedDepth()
    {
        QSignalSpy spy(m_watcher.get(), &FolderWatcher::changed);
        // A folder at the depth limit can only hold files deeper than discovery scans.
        QVERIFY(QDir(m_root.path()).mkpath(QStringLiteral("Tools/Editor/tmp-lock")));
        QVERIFY(QDir(m_root.filePath(QStringLiteral("Tools/Editor/tmp-lock"))).removeRecursively());
        QVERIFY(QDir(m_root.path()).mkpath(QStringLiteral("Tools/Editor/plugins/deep")));
        write(m_root.filePath(QStringLiteral("Tools/Editor/plugins/deep/Helper.exe")));
        QVERIFY(!settle(spy));
    }

    void ignoresUnrelatedWrites()
    {
        write(m_root.filePath(QStringLiteral("Tools/Editor/settings.json")));
        QTest::qWait(300);
        QSignalSpy spy(m_watcher.get(), &FolderWatcher::changed);
        write(m_root.filePath(QStringLiteral("Tools/Editor/settings.json")), "more");
        write(m_root.filePath(QStringLiteral("Tools/cache")), "blob");
        QFile::remove(m_root.filePath(QStringLiteral("Tools/Editor/settings.json")));
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

    void recoversRootsThatAppearLater()
    {
        QTemporaryDir parent;
        const QString later = parent.filePath(QStringLiteral("MountedLater"));
        FolderWatcher watcher;
        watcher.setRetryInterval(200);
        watcher.setFolders({{later, -1}}, {});
        QCOMPARE(watcher.activeCount(), 0);

        QSignalSpy spy(&watcher, &FolderWatcher::changed);
        QVERIFY(QDir().mkpath(later));
        QVERIFY(spy.wait(3000)); // reported once the folder can be watched
        QCOMPARE(watcher.activeCount(), 1);

        // Deleting the root releases it, so a folder with the same name can be recreated
        // and is then watched again.
        spy.clear();
        QVERIFY(QDir(later).removeRecursively());
        QTRY_VERIFY_WITH_TIMEOUT(watcher.activeCount() == 0 || !spy.isEmpty(), 3000);
        QVERIFY(QDir().mkpath(later));
        QTRY_COMPARE_WITH_TIMEOUT(watcher.activeCount(), 1, 3000);
        spy.clear();
        write(later + QStringLiteral("/New.exe"));
        QVERIFY(spy.wait(3000));
    }

    void noticesRootsMovedAway()
    {
        // Explorer's delete moves a folder into the Recycle Bin: the handle follows it and
        // stays valid, so the move is only visible through the handle's current path.
        QTemporaryDir parent;
        const QString root = parent.filePath(QStringLiteral("PortableApps"));
        QVERIFY(QDir().mkpath(root));
        FolderWatcher watcher;
        watcher.setRetryInterval(200);
        watcher.setFolders({{root, 3}}, {});
        QCOMPARE(watcher.activeCount(), 1);

        QSignalSpy spy(&watcher, &FolderWatcher::changed);
        QVERIFY(QDir(parent.path()).rename(QStringLiteral("PortableApps"), QStringLiteral("Recycled")));
        QVERIFY(spy.wait(3000));
        QTRY_COMPARE_WITH_TIMEOUT(watcher.activeCount(), 0, 3000);

        // A new folder with the original name is picked up and watched.
        spy.clear();
        QVERIFY(QDir().mkpath(root));
        QTRY_COMPARE_WITH_TIMEOUT(watcher.activeCount(), 1, 3000);
        spy.clear();
        write(root + QStringLiteral("/Fresh.exe"));
        QVERIFY(spy.wait(3000));
    }

    void stopsWatchingWhenCleared()
    {
        m_watcher->setFolders({}, {});
        QCOMPARE(m_watcher->activeCount(), 0);
        QSignalSpy spy(m_watcher.get(), &FolderWatcher::changed);
        write(m_root.filePath(QStringLiteral("Later.exe")));
        QVERIFY(!settle(spy));
    }
};

QTEST_MAIN(WatcherTest)
#include "tst_watcher.moc"
