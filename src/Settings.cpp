#include "Settings.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

namespace {

constexpr auto kRunKey = "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr auto kRunValue = "Omnipane";

QString startupCommand()
{
    return QStringLiteral("\"%1\" --background")
        .arg(QDir::toNativeSeparators(QCoreApplication::applicationFilePath()));
}

} // namespace

Settings::Settings()
    : m_settings(dataDirectory() + QStringLiteral("/omnipane.ini"), QSettings::IniFormat)
{
}

QString Settings::dataDirectory()
{
    // OMNIPANE_DATA_DIR lets the test suite (or a portable setup) keep settings and cache elsewhere.
    // Relative values are taken relative to Omnipane.exe, not the working directory (which is
    // System32 when started from the Run key).
    const QString overridden = qEnvironmentVariable("OMNIPANE_DATA_DIR");
    if (!overridden.isEmpty()) {
        const QString dir = QDir::cleanPath(QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(overridden));
        QDir().mkpath(dir);
        return dir;
    }

    const QString base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    const QString dir = base + QStringLiteral("/Omnipane");
    if (!QFileInfo::exists(dir))
        migrateLegacySettings(base + QStringLiteral("/Nexora/nexora.ini"), dir + QStringLiteral("/omnipane.ini"));
    QDir().mkpath(dir);
    return dir;
}

void Settings::migrateLegacySettings(const QString& legacyIni, const QString& newIni)
{
    // Omnipane was called Nexora before; carry the user's hotkey, custom apps and folders,
    // hidden apps and preferences over on first start. The icon cache is rebuilt instead.
    if (!QFileInfo::exists(legacyIni))
        return;
    QDir().mkpath(QFileInfo(newIni).absolutePath());
    QFile::copy(legacyIni, newIni);
}

QString Settings::hotkey() const
{
    return m_settings.value("hotkey", QStringLiteral("Ctrl+Space")).toString();
}

void Settings::setHotkey(const QString& sequence)
{
    m_settings.setValue("hotkey", sequence);
}

QStringList Settings::customFolders() const { return list("customFolders"); }
void Settings::addCustomFolder(const QString& path) { appendUnique("customFolders", path); }

QStringList Settings::customApps() const { return list("customApps"); }
void Settings::addCustomApp(const QString& path) { appendUnique("customApps", path); }

QStringList Settings::hiddenApps() const { return list("hiddenApps"); }
void Settings::hideApp(const QString& id) { appendUnique("hiddenApps", id); }
void Settings::clearHiddenApps() { m_settings.remove("hiddenApps"); }

bool Settings::dimDesktop() const
{
    return m_settings.value("dimDesktop", true).toBool();
}

void Settings::setDimDesktop(bool enabled)
{
    m_settings.setValue("dimDesktop", enabled);
}

bool Settings::launchAtStartup()
{
    const QSettings run(kRunKey, QSettings::NativeFormat);
    return run.value(kRunValue).toString().compare(startupCommand(), Qt::CaseInsensitive) == 0;
}

void Settings::setLaunchAtStartup(bool enabled)
{
    QSettings run(kRunKey, QSettings::NativeFormat);
    if (enabled)
        run.setValue(kRunValue, startupCommand());
    else
        run.remove(kRunValue);
}

QStringList Settings::list(const QString& key) const
{
    return m_settings.value(key).toStringList();
}

void Settings::appendUnique(const QString& key, const QString& value)
{
    QStringList values = list(key);
    if (values.contains(value, Qt::CaseInsensitive))
        return;
    values.append(value);
    m_settings.setValue(key, values);
}
