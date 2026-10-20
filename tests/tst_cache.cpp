// Cache persistence: round trip, and rejection of corrupt or outdated files.

#include "AppCache.h"
#include "Settings.h"

#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

class CacheTest : public QObject {
    Q_OBJECT

private:
    QString cacheFile() const { return Settings::dataDirectory() + QStringLiteral("/apps.cache"); }

    void writeRaw(quint32 magic, quint32 version, quint8 kind)
    {
        QFile file(cacheFile());
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QDataStream out(&file);
        out.setVersion(QDataStream::Qt_6_5);
        out << magic << version << quint32(1);
        out << QString("id") << QString("name") << kind << quint8(0) << QString("t") << QString() << QString("k")
            << qint64(0) << QImage();
    }

    QTemporaryDir m_dir;

private slots:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        // Keep the user's real cache untouched.
        qputenv("OMNIPANE_DATA_DIR", m_dir.path().toUtf8());
        QCOMPARE(Settings::dataDirectory(), m_dir.path());
    }

    void roundTrip()
    {
        QImage icon(96, 96, QImage::Format_ARGB32_Premultiplied);
        icon.fill(QColor(139, 92, 246));

        AppEntry packaged;
        packaged.id = QStringLiteral("pkg:microsoft.windowscalculator_8wekyb3d8bbwe!app");
        packaged.name = QStringLiteral("Calculator");
        packaged.kind = AppKind::Packaged;
        packaged.source = AppSource::Packaged;
        packaged.target = QStringLiteral("Microsoft.WindowsCalculator_8wekyb3d8bbwe!App");
        packaged.iconKey = QStringLiteral("shell:AppsFolder\\") + packaged.target;
        packaged.iconStamp = 1234567;
        packaged.icon = icon;

        AppEntry shortcut;
        shortcut.id = QStringLiteral("app:c:/tools/ünïcode.exe|--flag");
        shortcut.name = QStringLiteral("Ünïcode Tool");
        shortcut.kind = AppKind::Shortcut;
        shortcut.source = AppSource::StartMenu;
        shortcut.target = QStringLiteral("C:\\Start\\Ünïcode Tool.lnk");
        shortcut.resolvedPath = QStringLiteral("C:\\tools\\ünïcode.exe");
        shortcut.iconKey = shortcut.target;

        QVERIFY(AppCache::save({packaged, shortcut}));
        const std::vector<AppEntry> loaded = AppCache::load();
        QCOMPARE(loaded.size(), size_t(2));

        QCOMPARE(loaded[0].id, packaged.id);
        QCOMPARE(loaded[0].name, packaged.name);
        QCOMPARE(loaded[0].kind, AppKind::Packaged);
        QCOMPARE(loaded[0].source, AppSource::Packaged);
        QCOMPARE(loaded[0].iconKey, packaged.iconKey);
        QCOMPARE(loaded[0].iconStamp, packaged.iconStamp);
        QCOMPARE(loaded[0].icon.size(), icon.size());
        QCOMPARE(loaded[0].icon.pixelColor(10, 10), QColor(139, 92, 246));

        QCOMPARE(loaded[1].name, shortcut.name);
        QCOMPARE(loaded[1].resolvedPath, shortcut.resolvedPath);
        QCOMPARE(loaded[1].kind, AppKind::Shortcut);
        QVERIFY(loaded[1].icon.isNull());
    }

    void rejectsGarbage()
    {
        QFile file(cacheFile());
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write("this is not a cache file");
        file.close();
        QVERIFY(AppCache::load().empty());
    }

    void rejectsOtherVersions()
    {
        writeRaw(0x4E58524Fu, 1, 0);
        QVERIFY(AppCache::load().empty());
    }

    void rejectsOutOfRangeEnums()
    {
        writeRaw(0x4E58524Fu, 2, 0);
        QCOMPARE(AppCache::load().size(), size_t(1)); // sanity: the hand-written format is valid
        writeRaw(0x4E58524Fu, 2, 9);
        QVERIFY(AppCache::load().empty());
    }

    void migratesSettingsFromTheFormerName()
    {
        const QString base = m_dir.filePath(QStringLiteral("migration"));
        const QString legacy = base + QStringLiteral("/Nexora/nexora.ini");
        QDir().mkpath(QFileInfo(legacy).absolutePath());
        {
            QSettings old(legacy, QSettings::IniFormat);
            old.setValue("hotkey", QStringLiteral("Alt+Space"));
            old.setValue("customFolders", QStringList{QStringLiteral("D:\\PortableApps")});
        }
        // Make it read-only: the copy must still be writable afterwards.
        QFile::setPermissions(legacy, QFileDevice::ReadOwner | QFileDevice::ReadUser);

        // 1. First start: the folder is created and settings are carried over.
        const QString dir = base + QStringLiteral("/Omnipane");
        QCOMPARE(Settings::prepareDataDirectory(dir, legacy), dir);
        const QString ini = dir + QStringLiteral("/omnipane.ini");
        {
            const QSettings migrated(ini, QSettings::IniFormat);
            QCOMPARE(migrated.value("hotkey").toString(), QStringLiteral("Alt+Space"));
            QCOMPARE(migrated.value("customFolders").toStringList(), QStringList{QStringLiteral("D:\\PortableApps")});
        }
        QVERIFY(QFileInfo(ini).isWritable());

        // 2. Later starts never overwrite Omnipane's own settings.
        {
            QSettings current(ini, QSettings::IniFormat);
            current.setValue("hotkey", QStringLiteral("Ctrl+Alt+O"));
        }
        Settings::prepareDataDirectory(dir, legacy);
        QCOMPARE(QSettings(ini, QSettings::IniFormat).value("hotkey").toString(), QStringLiteral("Ctrl+Alt+O"));

        // 3. A folder that exists but has no settings yet (e.g. an earlier failed copy) is retried.
        QFile::remove(ini);
        Settings::prepareDataDirectory(dir, legacy);
        QCOMPARE(QSettings(ini, QSettings::IniFormat).value("hotkey").toString(), QStringLiteral("Alt+Space"));

        // 4. Nothing to migrate: just an empty folder.
        const QString fresh = base + QStringLiteral("/Fresh");
        Settings::prepareDataDirectory(fresh, base + QStringLiteral("/missing.ini"));
        QVERIFY(QFileInfo(fresh).isDir());
        QVERIFY(!QFileInfo::exists(fresh + QStringLiteral("/omnipane.ini")));

        // 5. A portable folder from NEXORA_DATA_DIR converts its own nexora.ini in place.
        const QString portable = base + QStringLiteral("/portable");
        QDir().mkpath(portable);
        QFile::copy(legacy, portable + QStringLiteral("/nexora.ini"));
        Settings::prepareDataDirectory(portable, portable + QStringLiteral("/nexora.ini"));
        QCOMPARE(QSettings(portable + QStringLiteral("/omnipane.ini"), QSettings::IniFormat).value("hotkey").toString(),
                 QStringLiteral("Alt+Space"));
        QFile::setPermissions(legacy, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    }

    void migratesAStaleNexoraAutostartEntry()
    {
        // A throwaway HKCU key stands in for ...\CurrentVersion\Run.
        const QString key = QStringLiteral("HKEY_CURRENT_USER\\Software\\OmnipaneTests\\Run");
        QSettings run(key, QSettings::NativeFormat);

        // Nexora still installed: leave its entry alone.
        const QString live = QStringLiteral("\"C:\\Windows\\System32\\notepad.exe\" --background");
        run.setValue("Nexora", live);
        run.sync();
        Settings::migrateLegacyAutostart(key);
        run.sync();
        QCOMPARE(run.value("Nexora").toString(), live);
        QVERIFY(!run.contains("Omnipane"));

        // Nexora's program is gone: the entry moves over to Omnipane.
        run.setValue("Nexora", QStringLiteral("\"C:\\Omnipane-test\\gone\\Nexora.exe\" --background"));
        run.sync();
        Settings::migrateLegacyAutostart(key);
        run.sync();
        QVERIFY(!run.contains("Nexora"));
        QVERIFY(run.value("Omnipane").toString().endsWith(QStringLiteral("\" --background")));

        QSettings(QStringLiteral("HKEY_CURRENT_USER\\Software"), QSettings::NativeFormat).remove("OmnipaneTests");
    }

    void missingFileIsEmpty()
    {
        QFile::remove(cacheFile());
        QVERIFY(AppCache::load().empty());
    }
};

QTEST_MAIN(CacheTest)
#include "tst_cache.moc"
