// Discovery against a synthetic custom folder plus the real Start menu, and icon extraction.

#include "AppDiscovery.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#include <algorithm>

class DiscoveryTest : public QObject {
    Q_OBJECT

private:
    void touch(const QString& relativePath, const QByteArray& contents = {})
    {
        const QString path = m_root.filePath(relativePath);
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(contents);
    }

    QStringList customFolderNames(const std::vector<AppEntry>& apps) const
    {
        QStringList names;
        for (const AppEntry& app : apps) {
            if (app.source == AppSource::CustomFolder)
                names.append(app.name);
        }
        names.sort();
        return names;
    }

    QTemporaryDir m_root;
    std::vector<AppEntry> m_apps;

private slots:
    void initTestCase()
    {
        QVERIFY(m_root.isValid());
        touch(QStringLiteral("Tool.exe"));
        touch(QStringLiteral("setup.exe"));         // installer noise
        touch(QStringLiteral("library.dll"));       // not an application
        touch(QStringLiteral("level1/Deep1.exe"));
        touch(QStringLiteral("level1/level2/Deep2.exe"));
        touch(QStringLiteral("level1/level2/level3/TooDeep.exe"));
        touch(QStringLiteral("Game.url"), "[InternetShortcut]\r\nURL=steam://rungameid/42\r\n");
        touch(QStringLiteral("Website.url"), "[InternetShortcut]\r\nURL=https://example.com/\r\n");

        QElapsedTimer timer;
        timer.start();
        m_apps = AppDiscovery::discover({{m_root.path()}, {}}, std::stop_token{});
        qInfo("full discovery: %lld ms, %zu apps", timer.elapsed(), m_apps.size());
    }

    void scansCustomFolderToTwoLevels()
    {
        QCOMPARE(customFolderNames(m_apps), QStringList({"Deep1", "Deep2", "Game", "Tool"}));
    }

    void keepsAppProtocolLinksOnly()
    {
        const auto game = std::find_if(m_apps.begin(), m_apps.end(), [](const AppEntry& a) { return a.name == "Game"; });
        QVERIFY(game != m_apps.end());
        QCOMPARE(game->id, QStringLiteral("url:steam://rungameid/42"));
        QCOMPARE(game->kind, AppKind::Shortcut);
    }

    void findsStartMenuApplications()
    {
        const bool any = std::any_of(m_apps.begin(), m_apps.end(),
                                     [](const AppEntry& a) { return a.source == AppSource::StartMenu; });
        QVERIFY2(any, "no Start menu applications found");
    }

    void producesUniqueSortedIds()
    {
        QSet<QString> ids;
        for (const AppEntry& app : m_apps) {
            QVERIFY2(!ids.contains(app.id), qPrintable(QStringLiteral("duplicate id ") + app.id));
            ids.insert(app.id);
        }
        QCollator collator;
        collator.setCaseSensitivity(Qt::CaseInsensitive);
        collator.setNumericMode(true);
        for (size_t i = 1; i < m_apps.size(); ++i)
            QVERIFY(collator.compare(m_apps[i - 1].name, m_apps[i].name) <= 0);
    }

    void stopsWhenRequested()
    {
        std::stop_source source;
        source.request_stop();
        QElapsedTimer timer;
        timer.start();
        const std::vector<AppEntry> apps = AppDiscovery::discover({{m_root.path()}, {}}, source.get_token());
        QVERIFY(timer.elapsed() < 1500);
        QVERIFY(std::none_of(apps.begin(), apps.end(),
                             [](const AppEntry& a) { return a.source == AppSource::StartMenu; }));
    }

    void buildsManualEntries()
    {
        touch(QStringLiteral("manual/run.cmd"), "@echo off\r\n");
        AppEntry entry;
        QVERIFY(AppDiscovery::entryForFile(m_root.filePath(QStringLiteral("manual/run.cmd")), AppSource::Manual, entry));
        QCOMPARE(entry.kind, AppKind::Executable);
        QCOMPARE(entry.name, QStringLiteral("run"));
        QVERIFY(!AppDiscovery::entryForFile(m_root.filePath(QStringLiteral("missing.exe")), AppSource::Manual, entry));
    }

    void watchesStartMenuAndCustomFolders()
    {
        const QStringList folders = AppDiscovery::watchedFolders({{m_root.path()}, {}});
        QVERIFY(folders.contains(QDir::cleanPath(m_root.path())));
        QVERIFY(std::any_of(folders.begin(), folders.end(),
                            [](const QString& f) { return f.endsWith(QStringLiteral("Start Menu/Programs")); }));
    }

    void extractsDesktopIcons()
    {
        const QImage icon = AppDiscovery::extractIcon(QStringLiteral("C:\\Windows\\System32\\notepad.exe"), 96);
        QCOMPARE(icon.size(), QSize(96, 96));
        QVERIFY(icon.hasAlphaChannel());
        QVERIFY(icon.pixelColor(48, 48).alpha() > 0);
    }

    void extractsPackagedLogos()
    {
        const auto packaged = std::find_if(m_apps.begin(), m_apps.end(),
                                           [](const AppEntry& a) { return a.kind == AppKind::Packaged; });
        if (packaged == m_apps.end())
            QSKIP("no packaged apps installed");
        const QImage icon = AppDiscovery::extractIcon(packaged->iconKey, 96);
        QCOMPARE(icon.size(), QSize(96, 96));
    }
};

QTEST_MAIN(DiscoveryTest)
#include "tst_discovery.moc"
