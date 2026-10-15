#include "FolderWatcher.h"

#include <QDir>
#include <QFileInfo>
#include <QWinEventNotifier>

#include <windows.h>

#include <algorithm>
#include <array>

namespace {

constexpr DWORD kNotifyFilter = FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE;

QString normalized(const QString& path)
{
    return QDir::cleanPath(QDir::fromNativeSeparators(path)).toLower();
}

} // namespace

struct FolderWatcher::Watch {
    QString root;
    HANDLE directory = INVALID_HANDLE_VALUE;
    HANDLE event = nullptr;
    OVERLAPPED overlapped = {};
    alignas(DWORD) std::array<std::byte, 32 * 1024> buffer = {};
    std::unique_ptr<QWinEventNotifier> notifier;
    bool pending = false;

    ~Watch()
    {
        notifier.reset();
        if (directory != INVALID_HANDLE_VALUE) {
            // The kernel writes into `buffer` until the request completes; wait for that.
            if (pending && CancelIoEx(directory, &overlapped)) {
                DWORD bytes = 0;
                GetOverlappedResult(directory, &overlapped, &bytes, TRUE);
            }
            CloseHandle(directory);
        }
        if (event)
            CloseHandle(event);
    }
};

FolderWatcher::FolderWatcher(QObject* parent)
    : QObject(parent)
{
}

FolderWatcher::~FolderWatcher() = default;

bool FolderWatcher::isRelevant(const QString& path, bool modifiedOnly)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    static const QStringList kAppSuffixes = {QStringLiteral("lnk"), QStringLiteral("url"),
                                             QStringLiteral("appref-ms"), QStringLiteral("exe")};
    if (kAppSuffixes.contains(suffix))
        return true;
    // Folders appearing, disappearing or being renamed can carry applications (deleted or
    // renamed-away paths no longer exist, so they cannot be inspected). Content writes to
    // other files, such as logs and settings of portable apps, never matter.
    if (modifiedOnly)
        return false;
    const QFileInfo info(path);
    return suffix.isEmpty() || info.isDir() || !info.exists();
}

void FolderWatcher::setFolders(const QStringList& roots, const QStringList& ignored)
{
    m_watches.clear();
    m_ignored.clear();
    for (const QString& path : ignored)
        m_ignored.append(normalized(path));

    for (const QString& root : roots) {
        auto watch = std::make_unique<Watch>();
        watch->root = QDir::cleanPath(QDir::fromNativeSeparators(root));
        const std::wstring native = QDir::toNativeSeparators(watch->root).toStdWString();
        watch->directory = CreateFileW(native.c_str(), FILE_LIST_DIRECTORY,
                                       FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                       OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
        watch->event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (watch->directory == INVALID_HANDLE_VALUE || !watch->event)
            continue;

        Watch* raw = watch.get();
        watch->notifier = std::make_unique<QWinEventNotifier>(watch->event);
        connect(watch->notifier.get(), &QWinEventNotifier::activated, this, [this, raw] { onSignaled(*raw); });
        arm(*watch);
        m_watches.push_back(std::move(watch));
    }
}

void FolderWatcher::arm(Watch& watch)
{
    ResetEvent(watch.event);
    watch.overlapped = {};
    watch.overlapped.hEvent = watch.event;
    watch.pending = ReadDirectoryChangesW(watch.directory, watch.buffer.data(), static_cast<DWORD>(watch.buffer.size()),
                                          TRUE, kNotifyFilter, nullptr, &watch.overlapped, nullptr)
        != FALSE;
}

void FolderWatcher::onSignaled(Watch& watch)
{
    DWORD bytes = 0;
    const bool ok = GetOverlappedResult(watch.directory, &watch.overlapped, &bytes, FALSE) != FALSE;
    watch.pending = false;
    if (!ok) {
        // The root itself was deleted or became inaccessible; stop watching it.
        watch.notifier->setEnabled(false);
        emit changed();
        return;
    }

    // Zero bytes means the buffer overflowed: too many changes to list, so assume relevance.
    bool relevant = bytes == 0;
    for (DWORD offset = 0; bytes != 0 && !relevant;) {
        const auto* info = reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(watch.buffer.data() + offset);
        const QString name = QString::fromWCharArray(info->FileName, info->FileNameLength / sizeof(wchar_t));
        const QString full = normalized(watch.root + u'/' + QDir::fromNativeSeparators(name));
        const bool ignored = std::any_of(m_ignored.begin(), m_ignored.end(), [&](const QString& prefix) {
            return full == prefix || full.startsWith(prefix + u'/');
        });
        if (!ignored && isRelevant(full, info->Action == FILE_ACTION_MODIFIED))
            relevant = true;
        if (info->NextEntryOffset == 0)
            break;
        offset += info->NextEntryOffset;
    }

    arm(watch);
    if (relevant)
        emit changed();
}
