#pragma once

#include <QSettings>
#include <QStringList>

// Small wrapper around Omnipane's INI file (%LOCALAPPDATA%\Omnipane\omnipane.ini).
class Settings {
public:
    Settings();

    static QString dataDirectory();

    // Creates `dir` and, the first time this process sees it without an omnipane.ini, copies
    // `legacyIni` (settings from the app's former name, Nexora) into it. Returns `dir`.
    static QString prepareDataDirectory(const QString& dir, const QString& legacyIni);
    static bool migrateLegacySettings(const QString& legacyIni, const QString& newIni);

    // Moves a Nexora "Start with Windows" entry to Omnipane when Nexora.exe was replaced by
    // Omnipane.exe in the same folder; leaves every other situation alone.
    static void migrateLegacyAutostart();
    static void migrateLegacyAutostart(const QString& runKey); // for tests

    QString hotkey() const;
    void setHotkey(const QString& sequence);

    QStringList customFolders() const;
    void addCustomFolder(const QString& path);

    QStringList customApps() const;
    void addCustomApp(const QString& path);

    QStringList hiddenApps() const;
    void hideApp(const QString& id);
    void clearHiddenApps();

    bool dimDesktop() const;
    void setDimDesktop(bool enabled);

    // Stored in HKCU\...\Run rather than in the INI file.
    static bool launchAtStartup();
    static void setLaunchAtStartup(bool enabled);

private:
    QStringList list(const QString& key) const;
    void appendUnique(const QString& key, const QString& value);

    QSettings m_settings;
};
