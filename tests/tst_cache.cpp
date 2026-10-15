// Cache persistence: round trip, and rejection of corrupt or outdated files.

#include "AppCache.h"
#include "Settings.h"

#include <QDataStream>
#include <QFile>
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
        qputenv("NEXORA_DATA_DIR", m_dir.path().toUtf8());
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

    void missingFileIsEmpty()
    {
        QFile::remove(cacheFile());
        QVERIFY(AppCache::load().empty());
    }
};

QTEST_MAIN(CacheTest)
#include "tst_cache.moc"
