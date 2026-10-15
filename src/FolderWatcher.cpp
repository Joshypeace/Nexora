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

// Where a directory handle currently points. A handle follows its folder when the folder
// is moved (e.g. into the Recycle Bin), so this changes if the watched root went away.
QString finalPath(HANDLE directory)
{
    std::array<wchar_t, 32768> buffer = {};
    const DWORD length =
        GetFinalPathNameByHandleW(directory, buffer.data(), static_cast<DWORD>(buffer.size()), FILE_NAME_NORMALIZED);
    if (length == 0 || length >= buffer.size())
        return {};
    return QString::fromWCharArray(buffer.data(), length).toLower();
}

bool hasAppSuffix(const QString& suffix)
{
    return suffix == u"lnk" || suffix == u"url" || suffix == u"appref-ms" || suffix == u"exe";
}

} // namespace

struct FolderWatcher::Watch {
    QString root;
    QString openedPath; // finalPath() when opened
    int maxDepth = -1;
    HANDLE directory = INVALID_HANDLE_VALUE;
    HANDLE event = nullptr;
    OVERLAPPED overlapped = {};
    alignas(DWORD) std::array<std::byte, 64 * 1024> buffer = {}; // 64 KB: the limit for network shares
    std::unique_ptr<QWinEventNotifier> notifier;
    bool pending = false;
    bool dead = false;

    void closeDirectory()
    {
        if (directory == INVALID_HANDLE_VALUE)
            return;
        // The kernel writes into `buffer` until the request completes; wait for that.
        if (pending && CancelIoEx(directory, &overlapped)) {
            DWORD bytes = 0;
            GetOverlappedResult(directory, &overlapped, &bytes, TRUE);
        }
        pending = false;
        CloseHandle(directory);
        directory = INVALID_HANDLE_VALUE;
    }

    ~Watch()
    {
        notifier.reset();
        closeDirectory();
        if (event)
            CloseHandle(event);
    }
};

FolderWatcher::FolderWatcher(QObject* parent)
    : QObject(parent)
{
    m_retry.setInterval(60 * 1000);
    connect(&m_retry, &QTimer::timeout, this, &FolderWatcher::retryInactive);
}

FolderWatcher::~FolderWatcher() = default;

bool FolderWatcher::isRelevant(const QString& path, bool modifiedOnly)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (hasAppSuffix(suffix))
        return true;
    // Content writes to other files (logs, caches, settings of portable apps) never matter.
    if (modifiedOnly)
        return false;
    // Folders appearing, disappearing or being renamed can carry applications. A path
    // that no longer exists (deleted, or a rename's old name) can't be inspected, so it
    // counts only if it could have been a folder; a vanished "x.tmp" was never an app.
    const QFileInfo info(path);
    return info.exists() ? info.isDir() : suffix.isEmpty();
}

void FolderWatcher::setFolders(const std::vector<Root>& roots, const QStringList& ignored)
{
    m_watches.clear();
    m_roots = roots;
    m_ignored.clear();
    for (const QString& path : ignored)
        m_ignored.append(normalized(path));

    for (const Root& root : m_roots)
        m_watches.push_back(open(root));

    // The periodic check also notices roots that were moved away (see retryInactive).
    if (m_roots.empty())
        m_retry.stop();
    else
        m_retry.start();
}

int FolderWatcher::activeCount() const
{
    return static_cast<int>(
        std::count_if(m_watches.begin(), m_watches.end(), [](const auto& watch) { return watch && !watch->dead; }));
}

std::unique_ptr<FolderWatcher::Watch> FolderWatcher::open(const Root& root)
{
    auto watch = std::make_unique<Watch>();
    watch->root = QDir::cleanPath(QDir::fromNativeSeparators(root.path));
    watch->maxDepth = root.maxDepth;
    const std::wstring native = QDir::toNativeSeparators(watch->root).toStdWString();
    watch->directory = CreateFileW(native.c_str(), FILE_LIST_DIRECTORY,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                                   FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
    watch->event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (watch->directory == INVALID_HANDLE_VALUE || !watch->event)
        return nullptr;
    watch->openedPath = finalPath(watch->directory);

    Watch* raw = watch.get();
    watch->notifier = std::make_unique<QWinEventNotifier>(watch->event);
    connect(watch->notifier.get(), &QWinEventNotifier::activated, this, [this, raw] { onSignaled(*raw); });
    arm(*watch);
    if (!watch->pending)
        return nullptr;
    return watch;
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

    bool relevant = false;
    if (ok && bytes == 0) {
        relevant = true; // the buffer overflowed: too many changes to list
    } else if (ok) {
        const QString rootKey = normalized(watch.root);
        for (DWORD offset = 0; !relevant;) {
            const auto* info = reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(watch.buffer.data() + offset);
            const QString name = QDir::fromNativeSeparators(
                QString::fromWCharArray(info->FileName, info->FileNameLength / sizeof(wchar_t)));
            const QString full = normalized(watch.root + u'/' + name);
            const bool ignored = std::any_of(m_ignored.begin(), m_ignored.end(), [&](const QString& prefix) {
                return full == prefix || full.startsWith(prefix + u'/');
            });
            // Discovery only looks so deep below custom folders; deeper churn is irrelevant.
            // Files count up to maxDepth segments; a folder at maxDepth could only hold
            // files deeper than that, so folder-like events must be shallower.
            const int depth = static_cast<int>(name.count(u'/')) + 1;
            const bool appFile = hasAppSuffix(QFileInfo(name).suffix().toLower());
            const bool tooDeep = watch.maxDepth >= 0 && (appFile ? depth > watch.maxDepth : depth >= watch.maxDepth);
            if (!ignored && !tooDeep && full != rootKey && isRelevant(full, info->Action == FILE_ACTION_MODIFIED))
                relevant = true;
            if (info->NextEntryOffset == 0)
                break;
            offset += info->NextEntryOffset;
        }
    }

    if (ok)
        arm(watch);
    if (!ok || !watch.pending) {
        // The root was deleted or became inaccessible. Release it (so a folder with the
        // same name can be recreated) and retry later; the notifier itself is destroyed
        // by the retry, not here inside its own signal.
        watch.notifier->setEnabled(false);
        watch.closeDirectory();
        watch.dead = true;
        m_retry.start();
        relevant = true;
    }

    if (relevant)
        emit changed();
}

void FolderWatcher::retryInactive()
{
    bool changedRoots = false;
    for (size_t i = 0; i < m_roots.size(); ++i) {
        std::unique_ptr<Watch>& current = m_watches[i];
        if (current && !current->dead) {
            // Still pointing where it was opened? A folder moved away (recycled, renamed)
            // keeps the handle valid, so only its path reveals that the root is gone.
            if (finalPath(current->directory) == current->openedPath)
                continue;
            current.reset();
            changedRoots = true; // its applications are gone from the original location
        }
        if (std::unique_ptr<Watch> watch = open(m_roots[i])) {
            current = std::move(watch);
            changedRoots = true; // a folder that (re)appeared may hold new applications
        }
    }
    if (changedRoots)
        emit changed();
}
