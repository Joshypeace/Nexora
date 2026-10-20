#pragma once

#include <QSettings>
#include <QStringList>

// Small wrapper around Omnipane's INI file (%LOCALAPPDATA%\Omnipane\omnipane.ini).
class Settings {
public:
    Settings();

    static QString dataDirectory();

    // Copies settings from the app's former name (Nexora) when Omnipane has none yet.
    static void migrateLegacySettings(const QString& legacyIni, const QString& newIni);

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
