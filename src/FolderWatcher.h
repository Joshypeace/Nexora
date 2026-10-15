#pragma once

#include <QObject>
#include <QStringList>

#include <memory>
#include <vector>

// Watches folder trees for changes that can affect discovery. Uses one recursive
// ReadDirectoryChangesW handle per root, opened with full sharing, so nothing below
// a root is ever locked (renaming or deleting subfolders keeps working).
class FolderWatcher : public QObject {
    Q_OBJECT

public:
    explicit FolderWatcher(QObject* parent = nullptr);
    ~FolderWatcher() override;

    FolderWatcher(const FolderWatcher&) = delete;
    FolderWatcher& operator=(const FolderWatcher&) = delete;

    // Replaces the watched roots. Changes at or below any `ignored` path are not reported.
    void setFolders(const QStringList& roots, const QStringList& ignored);

    // True if a change to `path` (with the given kind) could add, remove or alter an app.
    static bool isRelevant(const QString& path, bool modifiedOnly);

signals:
    void changed();

private:
    struct Watch;
    void arm(Watch& watch);
    void onSignaled(Watch& watch);

    std::vector<std::unique_ptr<Watch>> m_watches;
    QStringList m_ignored;
};
