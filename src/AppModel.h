#pragma once

#include "AppEntry.h"

#include <QAbstractListModel>
#include <QHash>
#include <QPixmap>
#include <QSet>
#include <QSortFilterProxyModel>

#include <vector>

// Flat list of applications with display-ready icons.
class AppModel : public QAbstractListModel {
    Q_OBJECT

public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        KindRole,
    };

    explicit AppModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;

    void setApps(const std::vector<AppEntry>& apps, qreal devicePixelRatio);
    void updateIcons(const QHash<QString, QImage>& icons, qreal devicePixelRatio);
    void setDevicePixelRatio(qreal devicePixelRatio);
    const AppEntry* entry(const QModelIndex& index) const;

    static constexpr int kIconSize = 48; // logical pixels

private:
    static QPixmap toPixmap(const QImage& image, qreal devicePixelRatio);

    std::vector<AppEntry> m_apps;
    std::vector<QPixmap> m_pixmaps;
    QHash<QString, int> m_rows;
};

// Case-insensitive search with relevance ordering and hidden-app filtering.
class AppFilterModel : public QSortFilterProxyModel {
    Q_OBJECT

public:
    explicit AppFilterModel(QObject* parent = nullptr);

    void setQuery(const QString& query);
    QString query() const { return m_query; }
    void setHiddenIds(const QStringList& ids);

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override;
    bool lessThan(const QModelIndex& left, const QModelIndex& right) const override;

private:
    int score(const QString& name) const;

    QString m_query;
    QStringList m_terms;
    QSet<QString> m_hidden;
};
