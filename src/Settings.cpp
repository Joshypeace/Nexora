#include "Settings.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QStandardPaths>

#include <windows.h>

#include <string>

namespace {

constexpr auto kRunKey = "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr auto kRunValue = "Omnipane";

QString expandEnvironment(const QString& value)
{
    const std::wstring input = value.toStdWString();
    const DWORD needed = ExpandEnvironmentStringsW(input.c_str(), nullptr, 0);
    if (needed == 0)
        return value;
    std::wstring output(needed, L'\0');
    const DWORD written = ExpandEnvironmentStringsW(input.c_str(), output.data(), needed);
    if (written == 0 || written > needed)
        return value;
    output.resize(written - 1);
    return QString::fromStdWString(output);
}

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
    // OMNIPANE_DATA_DIR lets the test suite (or a portable setup) keep settings and cache elsewhere;
    // NEXORA_DATA_DIR is the variable's former name and still honoured. Relative values are taken
    // relative to Omnipane.exe, not the working directory (System32 when started from the Run key).
    QString overridden = qEnvironmentVariable("OMNIPANE_DATA_DIR");
    if (overridden.isEmpty())
        overridden = qEnvironmentVariable("NEXORA_DATA_DIR");
    if (!overridden.isEmpty()) {
        const QString dir = QDir::cleanPath(QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(overridden));
        return prepareDataDirectory(dir, dir + QStringLiteral("/nexora.ini"));
    }

    const QString base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    return prepareDataDirectory(base + QStringLiteral("/Omnipane"), base + QStringLiteral("/Nexora/nexora.ini"));
}

QString Settings::prepareDataDirectory(const QString& dir, const QString& legacyIni)
{
    QDir().mkpath(dir);
    // Once per folder per process: copying later would happen underneath the live QSettings.
    // Keyed on the settings file rather than the folder, so a failed copy is retried at the
    // next start.
    static QSet<QString> checked;
    const QString ini = dir + QStringLiteral("/omnipane.ini");
    if (!checked.contains(ini)) {
        checked.insert(ini);
        if (!QFileInfo::exists(ini))
            migrateLegacySettings(legacyIni, ini);
    }
    return dir;
}

bool Settings::migrateLegacySettings(const QString& legacyIni, const QString& newIni)
{
    // Omnipane was called Nexora before; carry the user's hotkey, custom apps and folders,
    // hidden apps and preferences over. The icon cache is rebuilt instead.
    if (!QFileInfo::exists(legacyIni))
        return false;
    QDir().mkpath(QFileInfo(newIni).absolutePath());
    if (!QFile::copy(legacyIni, newIni))
        return false;
    // QFile::copy keeps a read-only attribute; Omnipane must be able to save its settings.
    QFile::setPermissions(newIni, QFile::permissions(newIni) | QFileDevice::WriteOwner | QFileDevice::WriteUser);
    return true;
}

void Settings::migrateLegacyAutostart()
{
    migrateLegacyAutostart(QString::fromLatin1(kRunKey));
}

void Settings::migrateLegacyAutostart(const QString& runKey)
{
    // A "Start with Windows" entry for a portable Nexora.exe that was swapped for Omnipane.exe
    // in the same folder would fail at every sign-in; point it at that Omnipane instead.
    // Only that exact situation is touched: an unplugged drive, an unusual command line or a
    // Nexora that is still present all leave the entry alone.
    QSettings run(runKey, QSettings::NativeFormat);
    const QString legacy = run.value("Nexora").toString().trimmed();
    if (!legacy.startsWith(u'"')) // Nexora always wrote a quoted absolute path
        return;
    const QFileInfo exe(expandEnvironment(legacy.section(u'"', 1, 1)));
    const QFileInfo replacement(exe.absolutePath() + QStringLiteral("/Omnipane.exe"));
    if (!exe.isAbsolute() || !exe.dir().exists() || exe.exists() || !replacement.isFile())
        return;
    run.remove("Nexora");
    if (!run.contains(kRunValue))
        run.setValue(kRunValue, QStringLiteral("\"%1\" --background").arg(QDir::toNativeSeparators(replacement.absoluteFilePath())));
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
