#include "AppModel.h"

#include <QCollator>

// ---------------------------------------------------------------------------
// AppModel
// ---------------------------------------------------------------------------

AppModel::AppModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

int AppModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_apps.size());
}

QVariant AppModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= rowCount())
        return {};

    const auto row = static_cast<size_t>(index.row());
    const AppEntry& app = m_apps[row];
    switch (role) {
    case Qt::DisplayRole:
    case Qt::ToolTipRole:
        return app.name;
    case Qt::DecorationRole:
        return m_pixmaps[row];
    case IdRole:
        return app.id;
    case KindRole:
        return static_cast<int>(app.kind);
    default:
        return {};
    }
}

void AppModel::setApps(const std::vector<AppEntry>& apps, qreal devicePixelRatio)
{
    beginResetModel();
    m_apps = apps;
    m_pixmaps.clear();
    m_pixmaps.reserve(m_apps.size());
    m_rows.clear();
    for (size_t i = 0; i < m_apps.size(); ++i) {
        m_pixmaps.push_back(toPixmap(m_apps[i].icon, devicePixelRatio));
        m_rows.insert(m_apps[i].id, static_cast<int>(i));
    }
    endResetModel();
}

void AppModel::updateIcons(const QHash<QString, QImage>& icons, qreal devicePixelRatio)
{
    for (auto it = icons.constBegin(); it != icons.constEnd(); ++it) {
        const auto row = m_rows.constFind(it.key());
        if (row == m_rows.constEnd())
            continue;
        const auto r = static_cast<size_t>(*row);
        m_apps[r].icon = it.value();
        m_pixmaps[r] = toPixmap(it.value(), devicePixelRatio);
        const QModelIndex changed = index(*row);
        emit dataChanged(changed, changed, {Qt::DecorationRole});
    }
}

void AppModel::setDevicePixelRatio(qreal devicePixelRatio)
{
    for (size_t i = 0; i < m_apps.size(); ++i)
        m_pixmaps[i] = toPixmap(m_apps[i].icon, devicePixelRatio);
    if (!m_apps.empty())
        emit dataChanged(index(0), index(rowCount() - 1), {Qt::DecorationRole});
}

const AppEntry* AppModel::entry(const QModelIndex& index) const
{
    if (!index.isValid() || index.row() >= rowCount())
        return nullptr;
    return &m_apps[static_cast<size_t>(index.row())];
}

QPixmap AppModel::toPixmap(const QImage& image, qreal devicePixelRatio)
{
    if (image.isNull())
        return {};
    const int pixels = qRound(kIconSize * devicePixelRatio);
    QPixmap pixmap = QPixmap::fromImage(image.size() == QSize(pixels, pixels)
                                            ? image
                                            : image.scaled(pixels, pixels, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    pixmap.setDevicePixelRatio(devicePixelRatio);
    return pixmap;
}

// ---------------------------------------------------------------------------
// AppFilterModel
// ---------------------------------------------------------------------------

namespace {

// Initials of each word, e.g. "Visual Studio Code" -> "vsc".
QString initials(const QString& lowerName)
{
    QString result;
    bool atWordStart = true;
    for (const QChar c : lowerName) {
        if (c.isLetterOrNumber()) {
            if (atWordStart)
                result.append(c);
            atWordStart = false;
        } else {
            atWordStart = true;
        }
    }
    return result;
}

bool matchesWordStart(const QString& lowerName, const QString& term)
{
    for (qsizetype pos = lowerName.indexOf(term); pos >= 0; pos = lowerName.indexOf(term, pos + 1)) {
        if (pos == 0 || !lowerName.at(pos - 1).isLetterOrNumber())
            return true;
    }
    return false;
}

} // namespace

AppFilterModel::AppFilterModel(QObject* parent)
    : QSortFilterProxyModel(parent)
{
    setDynamicSortFilter(true);
    sort(0);
}

void AppFilterModel::setQuery(const QString& query)
{
    const QString normalized = query.simplified().toLower();
    if (normalized == m_query)
        return;
    m_query = normalized;
    m_terms = normalized.split(u' ', Qt::SkipEmptyParts);
    invalidate();
}

void AppFilterModel::setHiddenIds(const QStringList& ids)
{
    m_hidden = QSet<QString>(ids.cbegin(), ids.cend());
    invalidate();
}

int AppFilterModel::score(const QString& name) const
{
    if (m_terms.isEmpty())
        return 1;

    const QString lower = name.toLower();
    // Every term must appear somewhere in the name; the first term decides relevance.
    for (const QString& term : m_terms) {
        if (!lower.contains(term) && !(m_terms.size() == 1 && initials(lower).startsWith(term)))
            return 0;
    }

    const QString& first = m_terms.constFirst();
    if (lower.startsWith(m_query))
        return 5;
    if (lower.startsWith(first))
        return 4;
    if (matchesWordStart(lower, first))
        return 3;
    if (m_terms.size() == 1 && initials(lower).startsWith(first))
        return 2;
    return 1;
}

bool AppFilterModel::filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const
{
    const QModelIndex index = sourceModel()->index(sourceRow, 0, sourceParent);
    if (m_hidden.contains(index.data(AppModel::IdRole).toString()))
        return false;
    return score(index.data(Qt::DisplayRole).toString()) > 0;
}

bool AppFilterModel::lessThan(const QModelIndex& left, const QModelIndex& right) const
{
    const QString leftName = left.data(Qt::DisplayRole).toString();
    const QString rightName = right.data(Qt::DisplayRole).toString();
    const int leftScore = score(leftName);
    const int rightScore = score(rightName);
    if (leftScore != rightScore)
        return leftScore > rightScore;

    static const QCollator collator = [] {
        QCollator c;
        c.setCaseSensitivity(Qt::CaseInsensitive);
        c.setNumericMode(true);
        return c;
    }();
    return collator.compare(leftName, rightName) < 0;
}
