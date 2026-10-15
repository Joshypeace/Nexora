#pragma once

#include <QStyledItemDelegate>

// Paints an application tile: icon, up to two lines of name, hover and selection states.
class AppDelegate : public QStyledItemDelegate {
    Q_OBJECT

public:
    using QStyledItemDelegate::QStyledItemDelegate;

    static constexpr QSize kCellSize{112, 108};

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;
};
