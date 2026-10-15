#pragma once

#include <QObject>
#include <QStringList>
#include <QTimer>

#include <memory>
#include <vector>

// Watches folder trees for changes that can affect discovery. Uses one recursive
// ReadDirectoryChangesW handle per root, opened with full sharing, so nothing below
// a root is ever locked (renaming or deleting subfolders keeps working). Roots that
// are missing (an unmounted drive) or stop working (deleted, recreated) are retried.
class FolderWatcher : public QObject {
    Q_OBJECT

public:
    struct Root {
        QString path;
        int maxDepth = -1; // path segments below the root that matter; -1 = unlimited
    };

    explicit FolderWatcher(QObject* parent = nullptr);
    ~FolderWatcher() override;

    FolderWatcher(const FolderWatcher&) = delete;
    FolderWatcher& operator=(const FolderWatcher&) = delete;

    // Replaces the watched roots. Changes at or below any `ignored` path are not reported.
    void setFolders(const std::vector<Root>& roots, const QStringList& ignored);

    // Number of roots currently being watched (missing or failed roots are not counted).
    int activeCount() const;

    // True if a change to `path` (with the given kind) could add, remove or alter an app.
    static bool isRelevant(const QString& path, bool modifiedOnly);

    // How often missing or failed roots are retried (default one minute).
    void setRetryInterval(int ms) { m_retry.setInterval(ms); }

signals:
    void changed();

private:
    struct Watch;
    std::unique_ptr<Watch> open(const Root& root);
    void arm(Watch& watch);
    void onSignaled(Watch& watch);
    void retryInactive();

    std::vector<Root> m_roots;
    std::vector<std::unique_ptr<Watch>> m_watches; // parallel to m_roots; null when inactive
    QStringList m_ignored;
    QTimer m_retry;
};
