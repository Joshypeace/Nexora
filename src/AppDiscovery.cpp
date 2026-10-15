#include "AppDiscovery.h"

#include <QCollator>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QPainter>
#include <QRegularExpression>
#include <QSet>
#include <QSettings>

#include <QXmlStreamReader>

#include <windows.h>
#include <appmodel.h>
#include <knownfolders.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <wrl/client.h>

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

using CoTaskString = std::unique_ptr<wchar_t, decltype(&CoTaskMemFree)>;

CoTaskString adoptCoTaskString(PWSTR raw)
{
    return CoTaskString(raw, &CoTaskMemFree);
}

QString pathKey(const QString& path)
{
    return QDir::cleanPath(QDir::fromNativeSeparators(path)).toLower();
}

QString nameKey(const QString& name)
{
    return name.simplified().toCaseFolded();
}

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

qint64 modificationStamp(const QString& path)
{
    return QFileInfo(path).lastModified().toMSecsSinceEpoch();
}

QString knownFolderPath(REFKNOWNFOLDERID id)
{
    PWSTR raw = nullptr;
    const HRESULT hr = SHGetKnownFolderPath(id, KF_FLAG_DEFAULT, nullptr, &raw);
    const CoTaskString path = adoptCoTaskString(raw);
    if (FAILED(hr) || !path)
        return {};
    return QDir::fromNativeSeparators(QString::fromWCharArray(path.get()));
}

QString itemDisplayName(IShellItem* item, SIGDN form)
{
    PWSTR raw = nullptr;
    if (FAILED(item->GetDisplayName(form, &raw)))
        return {};
    const CoTaskString name = adoptCoTaskString(raw);
    return QString::fromWCharArray(name.get());
}

// Localised display name the shell shows for a file (honours desktop.ini, hides ".lnk").
QString shellDisplayName(const QString& path)
{
    ComPtr<IShellItem> item;
    const std::wstring native = QDir::toNativeSeparators(path).toStdWString();
    if (FAILED(SHCreateItemFromParsingName(native.c_str(), nullptr, IID_PPV_ARGS(&item))))
        return {};
    return itemDisplayName(item.Get(), SIGDN_NORMALDISPLAY).trimmed();
}

QString versionString(const QString& path, const wchar_t* field)
{
    const std::wstring native = QDir::toNativeSeparators(path).toStdWString();
    DWORD handle = 0;
    const DWORD size = GetFileVersionInfoSizeW(native.c_str(), &handle);
    if (size == 0)
        return {};

    std::vector<std::byte> data(size);
    if (!GetFileVersionInfoW(native.c_str(), 0, size, data.data()))
        return {};

    struct LangCodePage {
        WORD language;
        WORD codePage;
    };
    LangCodePage* translations = nullptr;
    UINT length = 0;
    if (!VerQueryValueW(data.data(), L"\\VarFileInfo\\Translation", reinterpret_cast<void**>(&translations), &length)
        || length < sizeof(LangCodePage)) {
        return {};
    }

    wchar_t query[128] = {};
    swprintf_s(query, L"\\StringFileInfo\\%04x%04x\\%s", translations[0].language, translations[0].codePage, field);
    wchar_t* value = nullptr;
    UINT valueLength = 0;
    if (!VerQueryValueW(data.data(), query, reinterpret_cast<void**>(&value), &valueLength) || valueLength <= 1)
        return {};
    return QString::fromWCharArray(value).trimmed();
}

// ---------------------------------------------------------------------------
// Packaged app logos
// ---------------------------------------------------------------------------

constexpr auto kAppsFolderPrefix = QLatin1StringView("shell:AppsFolder\\");

QString packageInstallPath(const QString& familyName)
{
    const std::wstring family = familyName.toStdWString();
    UINT32 count = 0;
    UINT32 bufferLength = 0;
    if (GetPackagesByPackageFamily(family.c_str(), &count, nullptr, &bufferLength, nullptr) != ERROR_INSUFFICIENT_BUFFER
        || count == 0) {
        return {};
    }
    std::vector<PWSTR> fullNames(count);
    std::vector<wchar_t> buffer(bufferLength);
    if (GetPackagesByPackageFamily(family.c_str(), &count, fullNames.data(), &bufferLength, buffer.data()) != ERROR_SUCCESS)
        return {};

    UINT32 pathLength = 0;
    if (GetPackagePathByFullName(fullNames[0], &pathLength, nullptr) != ERROR_INSUFFICIENT_BUFFER)
        return {};
    std::vector<wchar_t> path(pathLength);
    if (GetPackagePathByFullName(fullNames[0], &pathLength, path.data()) != ERROR_SUCCESS)
        return {};
    return QDir::fromNativeSeparators(QString::fromWCharArray(path.data()));
}

struct ManifestLogo {
    QString relativePath;
    bool transparentBackground = false;
};

ManifestLogo manifestLogo(const QString& packagePath, const QString& appId)
{
    QFile file(packagePath + QStringLiteral("/AppxManifest.xml"));
    if (!file.open(QIODevice::ReadOnly))
        return {};

    QXmlStreamReader xml(&file);
    bool inApplication = false;
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement() && xml.name() == u"Application")
            inApplication = xml.attributes().value(u"Id") == appId;
        else if (xml.isEndElement() && xml.name() == u"Application")
            inApplication = false;
        else if (inApplication && xml.isStartElement() && xml.name() == u"VisualElements") {
            const QXmlStreamAttributes attributes = xml.attributes();
            ManifestLogo logo;
            logo.relativePath = attributes.value(u"Square44x44Logo").toString();
            if (logo.relativePath.isEmpty())
                logo.relativePath = attributes.value(u"Square150x150Logo").toString();
            logo.transparentBackground =
                attributes.value(u"BackgroundColor").compare(u"transparent", Qt::CaseInsensitive) == 0;
            return logo;
        }
    }
    return {};
}

// Picks the asset variant Start would use: unplated artwork, close to the requested size.
QString bestLogoVariant(const QString& packagePath, const ManifestLogo& logo, int size)
{
    const QFileInfo declared(packagePath + u'/' + QDir::fromNativeSeparators(logo.relativePath));
    const QString base = declared.completeBaseName().toLower();
    const QString suffix = u'.' + declared.suffix().toLower();
    const QDir directory = declared.absoluteDir();

    static const QRegularExpression kTargetSize(QStringLiteral("targetsize-(\\d+)"));
    static const QRegularExpression kScale(QStringLiteral("scale-(\\d+)"));

    QString best;
    int bestScore = -1;
    const QStringList files = directory.entryList({declared.completeBaseName() + QStringLiteral("*")}, QDir::Files);
    for (const QString& fileName : files) {
        const QString lower = fileName.toLower();
        if (!lower.startsWith(base) || !lower.endsWith(suffix) || lower.contains(u"contrast-")
            || lower.contains(u"lightunplated")) {
            continue;
        }

        const bool unplated = lower.contains(u"altform-unplated");
        if (!unplated && !logo.transparentBackground)
            continue; // plated artwork expects the shell's coloured tile

        int pixels = 44;
        if (const QRegularExpressionMatch match = kTargetSize.match(lower); match.hasMatch())
            pixels = match.captured(1).toInt();
        else if (const QRegularExpressionMatch scale = kScale.match(lower); scale.hasMatch())
            pixels = 44 * scale.captured(1).toInt() / 100;

        // Prefer the smallest asset at least as large as requested, then the largest smaller one.
        const int fit = pixels >= size ? 2000 - pixels : pixels;
        const int score = (unplated ? 10000 : 0) + fit;
        if (score > bestScore) {
            bestScore = score;
            best = directory.filePath(fileName);
        }
    }
    return best;
}

QImage packagedLogo(const QString& aumid, int size)
{
    const qsizetype bang = aumid.indexOf(u'!');
    if (bang <= 0)
        return {};
    const QString packagePath = packageInstallPath(aumid.left(bang));
    if (packagePath.isEmpty())
        return {};

    const ManifestLogo logo = manifestLogo(packagePath, aumid.mid(bang + 1));
    if (logo.relativePath.isEmpty())
        return {};
    const QString path = bestLogoVariant(packagePath, logo, size);
    QImage image(path);
    if (path.isEmpty() || image.isNull())
        return {};

    image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied)
                .scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QImage canvas(size, size, QImage::Format_ARGB32_Premultiplied);
    canvas.fill(Qt::transparent);
    QPainter painter(&canvas);
    painter.drawImage((size - image.width()) / 2, (size - image.height()) / 2, image);
    return canvas;
}

// ---------------------------------------------------------------------------
// Noise filters
// ---------------------------------------------------------------------------

bool containsAny(const QString& lowerText, std::initializer_list<QStringView> needles)
{
    return std::any_of(needles.begin(), needles.end(),
                       [&](QStringView needle) { return lowerText.contains(needle); });
}

// Start menu entries that are not applications in the launcher sense.
bool isNoiseName(const QString& name)
{
    const QString lower = name.toLower();
    return containsAny(lower, {u"uninstall", u"uninst", u"readme", u"read me", u"release notes", u"license",
                               u"licence", u"documentation", u"changelog", u"what's new", u"repair "});
}

// Executables found outside the Start menu that are rarely meant to be launched directly.
bool isNoiseExecutable(const QString& fileName)
{
    const QString lower = fileName.toLower();
    return containsAny(lower, {u"unins", u"setup", u"install", u"update", u"crash", u"helper", u"elevat",
                               u"redist", u"report", u"service", u"host", u"daemon", u"agent", u"notif",
                               u"cleanup", u"repair", u"diagnos", u"vc_", u"dotnet"});
}

bool isNoiseRegistryName(const QString& name)
{
    const QString lower = name.toLower();
    return containsAny(lower, {u"redistributable", u"runtime", u"visual c++", u"driver", u"update for",
                               u"hotfix", u"service pack", u"sdk", u"framework", u"targeting pack",
                               u"language pack", u"prerequisite", u"bootstrapper", u"plug-in", u"plugin"});
}

bool isLaunchableTarget(const QFileInfo& target)
{
    static const QSet<QString> kExtensions = {
        QStringLiteral("exe"), QStringLiteral("msc"), QStringLiteral("cpl"),
        QStringLiteral("bat"), QStringLiteral("cmd"), QStringLiteral("com"),
    };
    return kExtensions.contains(target.suffix().toLower());
}

// ---------------------------------------------------------------------------
// Entry builders
// ---------------------------------------------------------------------------

struct ShortcutTarget {
    QString path;
    QString arguments;
};

std::optional<ShortcutTarget> readShortcut(const QString& lnkPath)
{
    ComPtr<IShellLinkW> link;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link))))
        return std::nullopt;

    ComPtr<IPersistFile> file;
    const std::wstring native = QDir::toNativeSeparators(lnkPath).toStdWString();
    if (FAILED(link.As(&file)) || FAILED(file->Load(native.c_str(), STGM_READ)))
        return std::nullopt;

    ShortcutTarget result;
    std::wstring buffer(4096, L'\0');
    if (link->GetPath(buffer.data(), static_cast<int>(buffer.size()), nullptr, 0) == S_OK)
        result.path = QString::fromWCharArray(buffer.c_str());

    std::fill(buffer.begin(), buffer.end(), L'\0');
    if (SUCCEEDED(link->GetArguments(buffer.data(), static_cast<int>(buffer.size()))))
        result.arguments = QString::fromWCharArray(buffer.c_str()).trimmed();

    // Windows Installer "advertised" shortcuts report a cached icon file under
    // %WINDIR%\Installer instead of the real program; the shell resolves them on launch.
    if (pathKey(result.path).contains(QStringLiteral("/windows/installer/")))
        result.path.clear();

    return result;
}

std::optional<AppEntry> shortcutEntry(const QString& lnkPath, AppSource source, bool filterNoise)
{
    const QFileInfo info(lnkPath);
    QString name = shellDisplayName(lnkPath);
    if (name.isEmpty())
        name = info.completeBaseName();
    if (filterNoise && isNoiseName(name))
        return std::nullopt;

    const std::optional<ShortcutTarget> link = readShortcut(lnkPath);
    if (!link)
        return std::nullopt;

    if (!link->path.isEmpty() && filterNoise) {
        const QFileInfo target(link->path);
        if (!target.exists() || target.isDir() || !isLaunchableTarget(target)
            || target.fileName().contains(QStringLiteral("unins"), Qt::CaseInsensitive)) {
            return std::nullopt;
        }
    }

    AppEntry entry;
    entry.name = name;
    entry.kind = AppKind::Shortcut;
    entry.source = source;
    entry.target = QDir::toNativeSeparators(info.absoluteFilePath());
    entry.resolvedPath = QDir::toNativeSeparators(link->path);
    entry.id = link->path.isEmpty()
        ? QStringLiteral("lnk:") + nameKey(name)
        : QStringLiteral("app:") + pathKey(link->path) + u'|' + link->arguments.simplified().toLower();
    entry.iconKey = entry.target;
    // Program updates usually replace the executable but leave the shortcut untouched.
    entry.iconStamp = std::max(modificationStamp(lnkPath), link->path.isEmpty() ? 0 : modificationStamp(link->path));
    return entry;
}

// Reads the URL of an Internet Shortcut (.url) file.
QString internetShortcutUrl(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text) || file.size() > 64 * 1024)
        return {};
    const QStringList lines = QString::fromLocal8Bit(file.readAll()).split(u'\n');
    for (const QString& line : lines) {
        const QString trimmed = line.trimmed();
        if (trimmed.startsWith(QStringLiteral("URL="), Qt::CaseInsensitive))
            return trimmed.mid(4);
    }
    return {};
}

// Start menu .url files launching an application protocol (steam://, com.epicgames.launcher://, ...)
// and ClickOnce .appref-ms files. Both are started by the shell through the file itself.
std::optional<AppEntry> shellFileEntry(const QFileInfo& info, AppSource source, bool filterNoise)
{
    QString name = shellDisplayName(info.absoluteFilePath());
    if (name.isEmpty())
        name = info.completeBaseName();
    if (filterNoise && isNoiseName(name))
        return std::nullopt;

    QString id = QStringLiteral("lnk:") + nameKey(name);
    if (info.suffix().compare(QStringLiteral("url"), Qt::CaseInsensitive) == 0) {
        const QString url = internetShortcutUrl(info.absoluteFilePath());
        const QString scheme = url.section(u':', 0, 0).toLower();
        // Web links in the Start menu are "visit our website" entries, not applications.
        static const QSet<QString> kWebSchemes = {QStringLiteral("http"), QStringLiteral("https"),
                                                  QStringLiteral("ftp"), QStringLiteral("file"),
                                                  QStringLiteral("mailto")};
        if (url.isEmpty() || !url.contains(u':') || (filterNoise && kWebSchemes.contains(scheme)))
            return std::nullopt;
        id = QStringLiteral("url:") + url.toLower();
    }

    AppEntry entry;
    entry.name = name;
    entry.kind = AppKind::Shortcut;
    entry.source = source;
    entry.target = QDir::toNativeSeparators(info.absoluteFilePath());
    entry.id = id;
    entry.iconKey = entry.target;
    entry.iconStamp = modificationStamp(info.absoluteFilePath());
    return entry;
}

AppEntry executableEntry(const QString& exePath, const QString& displayName, AppSource source)
{
    const QFileInfo info(exePath);
    QString name = displayName;
    if (name.isEmpty()) {
        name = versionString(exePath, L"FileDescription");
        if (name.isEmpty() || name.size() > 48)
            name = info.completeBaseName();
    }

    AppEntry entry;
    entry.name = name;
    entry.kind = AppKind::Executable;
    entry.source = source;
    entry.target = QDir::toNativeSeparators(info.absoluteFilePath());
    entry.resolvedPath = entry.target;
    entry.id = QStringLiteral("app:") + pathKey(exePath) + u'|';
    entry.iconKey = entry.target;
    entry.iconStamp = modificationStamp(exePath);
    return entry;
}

// Builds an entry for any supported file type; `filterNoise` drops non-application files.
std::optional<AppEntry> fileEntry(const QFileInfo& info, AppSource source, bool filterNoise)
{
    const QString suffix = info.suffix().toLower();
    if (suffix == u"lnk")
        return shortcutEntry(info.absoluteFilePath(), source, filterNoise);
    if (suffix == u"url" || suffix == u"appref-ms")
        return shellFileEntry(info, source, filterNoise);
    if (suffix == u"exe")
        return filterNoise && isNoiseExecutable(info.fileName())
            ? std::nullopt
            : std::optional<AppEntry>(executableEntry(info.absoluteFilePath(), {}, source));
    if (filterNoise)
        return std::nullopt;
    return executableEntry(info.absoluteFilePath(), {}, source); // user-chosen .bat, .cmd, ...
}

// ---------------------------------------------------------------------------
// Deduplication
// ---------------------------------------------------------------------------

class Collector {
public:
    void add(AppEntry entry)
    {
        entry.name = entry.name.simplified();
        if (entry.name.isEmpty() || m_ids.contains(entry.id))
            return;

        const QString name = nameKey(entry.name);
        const QString path = entry.resolvedPath.isEmpty() ? QString() : pathKey(entry.resolvedPath);

        // Secondary sources only fill gaps: skip anything the user already sees by name or by program.
        const bool secondary = entry.source == AppSource::Packaged || entry.source == AppSource::Registry;
        if (secondary && (m_names.contains(name) || (!path.isEmpty() && m_paths.contains(path))))
            return;

        // Several shortcuts with the same name launching the same program are one application.
        const QString namePath = name + u'|' + path;
        if (!path.isEmpty() && m_namePaths.contains(namePath))
            return;

        m_ids.insert(entry.id);
        m_names.insert(name);
        if (!path.isEmpty()) {
            m_paths.insert(path);
            m_namePaths.insert(namePath);
        }
        m_apps.push_back(std::move(entry));
    }

    std::vector<AppEntry> take() { return std::move(m_apps); }

private:
    std::vector<AppEntry> m_apps;
    QSet<QString> m_ids;
    QSet<QString> m_names;
    QSet<QString> m_paths;
    QSet<QString> m_namePaths;
};

// ---------------------------------------------------------------------------
// Sources
// ---------------------------------------------------------------------------

void scanStartMenu(const QString& root, Collector& collector, const std::stop_token& stop)
{
    if (root.isEmpty())
        return;

    QDirIterator it(root, {QStringLiteral("*.lnk"), QStringLiteral("*.url"), QStringLiteral("*.appref-ms")},
                    QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext() && !stop.stop_requested()) {
        const QString path = it.next();
        // Startup items duplicate regular entries and are not meant to be launched by hand.
        if (path.contains(QStringLiteral("/Startup/"), Qt::CaseInsensitive))
            continue;
        if (std::optional<AppEntry> entry = fileEntry(it.fileInfo(), AppSource::StartMenu, true))
            collector.add(std::move(*entry));
    }
}

void scanPackagedApps(Collector& collector, const std::stop_token& stop)
{
    ComPtr<IShellItem> folder;
    if (FAILED(SHGetKnownFolderItem(FOLDERID_AppsFolder, KF_FLAG_DEFAULT, nullptr, IID_PPV_ARGS(&folder))))
        return;

    ComPtr<IEnumShellItems> items;
    if (FAILED(folder->BindToHandler(nullptr, BHID_EnumItems, IID_PPV_ARGS(&items))))
        return;

    ComPtr<IShellItem> item;
    while (!stop.stop_requested() && items->Next(1, item.ReleaseAndGetAddressOf(), nullptr) == S_OK) {
        // Packaged apps are identified by "<PackageFamilyName>!<AppId>"; other entries in
        // the Apps folder are desktop programs already covered by the Start menu scan.
        const QString aumid = itemDisplayName(item.Get(), SIGDN_PARENTRELATIVEPARSING);
        const qsizetype bang = aumid.indexOf(u'!');
        if (bang <= 0 || !aumid.left(bang).contains(u'_'))
            continue;

        AppEntry entry;
        entry.name = itemDisplayName(item.Get(), SIGDN_NORMALDISPLAY);
        entry.kind = AppKind::Packaged;
        entry.source = AppSource::Packaged;
        entry.target = aumid;
        entry.id = QStringLiteral("pkg:") + aumid.toLower();
        entry.iconKey = kAppsFolderPrefix + aumid;
        const QString packagePath = packageInstallPath(aumid.left(bang));
        if (!packagePath.isEmpty())
            entry.iconStamp = modificationStamp(packagePath + QStringLiteral("/AppxManifest.xml"));
        collector.add(std::move(entry));
    }
}

QString executableFromDisplayIcon(const QString& displayIcon)
{
    QString path = displayIcon.trimmed();
    // Strip an icon index (",0") and surrounding quotes.
    const qsizetype comma = path.lastIndexOf(u',');
    if (comma > 0 && !path.mid(comma + 1).contains(u'\\'))
        path.truncate(comma);
    path = expandEnvironment(path.trimmed().remove(u'"'));

    const QFileInfo info(path);
    if (info.suffix().compare(QStringLiteral("exe"), Qt::CaseInsensitive) != 0 || !info.isFile())
        return {};
    if (isNoiseExecutable(info.fileName()) || pathKey(path).contains(QStringLiteral("/windows/installer/")))
        return {};
    return info.absoluteFilePath();
}

void scanUninstallRegistry(Collector& collector, const std::stop_token& stop)
{
    struct Hive {
        const char* key;
        QSettings::Format format;
    };
    static constexpr auto kUninstall = "\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall";
    const Hive hives[] = {
        {"HKEY_CURRENT_USER", QSettings::Registry64Format},
        {"HKEY_LOCAL_MACHINE", QSettings::Registry64Format},
        {"HKEY_LOCAL_MACHINE", QSettings::Registry32Format},
    };

    for (const Hive& hive : hives) {
        QSettings registry(QString::fromLatin1(hive.key) + QString::fromLatin1(kUninstall), hive.format);
        const QStringList keys = registry.childGroups();
        for (const QString& key : keys) {
            if (stop.stop_requested())
                return;
            registry.beginGroup(key);
            const QString name = registry.value("DisplayName").toString().trimmed();
            const bool skip = name.isEmpty() || isNoiseRegistryName(name)
                || registry.value("SystemComponent").toInt() == 1
                || !registry.value("ParentKeyName").toString().isEmpty()
                || !registry.value("ReleaseType").toString().isEmpty();
            const QString exe = skip ? QString() : executableFromDisplayIcon(registry.value("DisplayIcon").toString());
            registry.endGroup();

            if (!exe.isEmpty())
                collector.add(executableEntry(exe, name, AppSource::Registry));
        }
    }
}

// Scans the folder and at most two levels of subfolders, breadth-first, without
// following links, so even a drive root costs a bounded amount of work.
void scanCustomFolder(const QString& folder, Collector& collector, const std::stop_token& stop)
{
    constexpr int kMaxDepth = 2;
    constexpr int kMaxVisited = 4000; // files and folders examined

    int visited = 0;
    std::vector<std::pair<QString, int>> pending{{QDir::cleanPath(QDir::fromNativeSeparators(folder)), 0}};
    for (size_t i = 0; i < pending.size(); ++i) {
        const auto [directory, depth] = pending[i]; // copy: `pending` grows below
        const QFileInfoList entries =
            QDir(directory).entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::DirsLast);
        for (const QFileInfo& info : entries) {
            if (stop.stop_requested() || ++visited > kMaxVisited)
                return;
            if (info.isDir()) {
                if (depth < kMaxDepth && !info.isSymLink() && !info.isJunction())
                    pending.emplace_back(info.absoluteFilePath(), depth + 1);
            } else if (std::optional<AppEntry> entry = fileEntry(info, AppSource::CustomFolder, true)) {
                collector.add(std::move(*entry));
            }
        }
    }
}

} // namespace

namespace AppDiscovery {

bool entryForFile(const QString& path, AppSource source, AppEntry& out)
{
    const QFileInfo info(path);
    if (!info.isFile())
        return false;

    std::optional<AppEntry> entry = fileEntry(info, source, false);
    if (!entry)
        return false;
    out = std::move(*entry);
    return true;
}

std::vector<AppEntry> discover(const Options& options, const std::stop_token& stop)
{
    Collector collector;

    for (const QString& path : options.customApps) {
        AppEntry entry;
        if (entryForFile(path, AppSource::Manual, entry))
            collector.add(std::move(entry));
    }
    for (const QString& folder : options.customFolders)
        scanCustomFolder(folder, collector, stop);

    scanStartMenu(knownFolderPath(FOLDERID_Programs), collector, stop);
    scanStartMenu(knownFolderPath(FOLDERID_CommonPrograms), collector, stop);
    scanPackagedApps(collector, stop);
    scanUninstallRegistry(collector, stop);

    std::vector<AppEntry> apps = collector.take();
    QCollator collator;
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    collator.setNumericMode(true);
    std::sort(apps.begin(), apps.end(),
              [&](const AppEntry& a, const AppEntry& b) { return collator.compare(a.name, b.name) < 0; });
    return apps;
}

QImage extractIcon(const QString& iconKey, int size)
{
    // The shell renders packaged apps on a coloured plate; prefer their own unplated artwork.
    if (iconKey.startsWith(kAppsFolderPrefix)) {
        QImage logo = packagedLogo(iconKey.mid(kAppsFolderPrefix.size()), size);
        if (!logo.isNull())
            return logo;
    }

    ComPtr<IShellItemImageFactory> factory;
    const std::wstring key = iconKey.toStdWString();
    if (FAILED(SHCreateItemFromParsingName(key.c_str(), nullptr, IID_PPV_ARGS(&factory))))
        return {};

    HBITMAP raw = nullptr;
    if (FAILED(factory->GetImage(SIZE{size, size}, SIIGBF_ICONONLY, &raw)) || !raw)
        return {};
    const std::unique_ptr<HBITMAP__, decltype(&DeleteObject)> bitmap(raw, &DeleteObject);

    BITMAP info = {};
    if (GetObjectW(raw, sizeof(info), &info) == 0 || info.bmWidth <= 0 || info.bmHeight <= 0)
        return {};

    QImage image(info.bmWidth, info.bmHeight, QImage::Format_ARGB32_Premultiplied);
    BITMAPINFO header = {};
    header.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    header.bmiHeader.biWidth = info.bmWidth;
    header.bmiHeader.biHeight = -info.bmHeight; // top-down rows
    header.bmiHeader.biPlanes = 1;
    header.bmiHeader.biBitCount = 32;
    header.bmiHeader.biCompression = BI_RGB;

    HDC screen = GetDC(nullptr);
    const int lines = GetDIBits(screen, raw, 0, static_cast<UINT>(info.bmHeight), image.bits(), &header, DIB_RGB_COLORS);
    ReleaseDC(nullptr, screen);
    if (lines != info.bmHeight)
        return {};

    // Legacy icons without an alpha channel come back fully transparent.
    const auto* pixels = reinterpret_cast<const quint32*>(image.constBits());
    const qsizetype count = qsizetype(image.width()) * image.height();
    const bool hasAlpha = std::any_of(pixels, pixels + count, [](quint32 p) { return (p >> 24) != 0; });
    if (!hasAlpha) {
        auto* mutablePixels = reinterpret_cast<quint32*>(image.bits());
        std::for_each(mutablePixels, mutablePixels + count, [](quint32& p) { p |= 0xFF000000u; });
    }
    return image;
}

} // namespace AppDiscovery
