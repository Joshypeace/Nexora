#include "AppDelegate.h"

#include "AppModel.h"

#include <QPainter>
#include <QTextLayout>

namespace {

constexpr int kRadius = 14;
constexpr int kIconTop = 14;
constexpr int kTextGap = 10;

// Wraps the name onto at most two lines, eliding the second one.
QStringList wrapName(const QString& text, const QFont& font, int width)
{
    QTextLayout layout(text, font);
    QTextOption option(Qt::AlignHCenter);
    option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    layout.setTextOption(option);

    QStringList lines;
    layout.beginLayout();
    QTextLine line = layout.createLine();
    if (line.isValid()) {
        line.setLineWidth(width);
        lines.append(text.mid(line.textStart(), line.textLength()).trimmed());
        const QTextLine second = layout.createLine();
        if (second.isValid())
            lines.append(QFontMetrics(font).elidedText(text.mid(second.textStart()).trimmed(), Qt::ElideRight, width));
    }
    layout.endLayout();
    return lines;
}

QPixmap placeholderIcon(int size, qreal dpr)
{
    QPixmap pixmap(QSize(size, size) * dpr);
    pixmap.setDevicePixelRatio(dpr);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 255, 255, 22));
    p.drawRoundedRect(QRectF(4, 4, size - 8, size - 8), 12, 12);
    return pixmap;
}

} // namespace

void AppDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setRenderHint(QPainter::SmoothPixmapTransform);

    const QRectF tile = QRectF(option.rect).adjusted(4, 4, -4, -4);
    const bool selected = option.state.testFlag(QStyle::State_Selected);
    const bool hovered = option.state.testFlag(QStyle::State_MouseOver);

    if (selected) {
        // Restrained violet -> blue -> cyan highlight with a hairline border.
        QLinearGradient fill(tile.topLeft(), tile.bottomRight());
        fill.setColorAt(0.0, QColor(139, 92, 246, 72));
        fill.setColorAt(0.55, QColor(59, 130, 246, 56));
        fill.setColorAt(1.0, QColor(34, 211, 238, 48));
        QLinearGradient border(tile.topLeft(), tile.bottomRight());
        border.setColorAt(0.0, QColor(167, 139, 250, 170));
        border.setColorAt(1.0, QColor(103, 232, 249, 130));
        painter->setPen(QPen(QBrush(border), 1.0));
        painter->setBrush(fill);
        painter->drawRoundedRect(tile.adjusted(0.5, 0.5, -0.5, -0.5), kRadius, kRadius);
    } else if (hovered) {
        painter->setPen(QPen(QColor(255, 255, 255, 20), 1.0));
        painter->setBrush(QColor(255, 255, 255, 14));
        painter->drawRoundedRect(tile.adjusted(0.5, 0.5, -0.5, -0.5), kRadius, kRadius);
    }

    const int iconSize = AppModel::kIconSize;
    const QRect iconRect(option.rect.center().x() - iconSize / 2 + 1, option.rect.top() + kIconTop, iconSize, iconSize);
    QPixmap icon = index.data(Qt::DecorationRole).value<QPixmap>();
    if (icon.isNull())
        icon = placeholderIcon(iconSize, painter->device()->devicePixelRatioF());
    painter->drawPixmap(iconRect, icon);

    QFont font = option.font;
    font.setPointSizeF(9.0);
    painter->setFont(font);
    painter->setPen(selected ? QColor(255, 255, 255) : QColor(214, 220, 240, 225));

    const int textWidth = static_cast<int>(tile.width()) - 12;
    const QStringList lines = wrapName(index.data(Qt::DisplayRole).toString(), font, textWidth);
    const QFontMetrics metrics(font);
    int y = iconRect.bottom() + kTextGap;
    for (const QString& line : lines) {
        const QRect lineRect(option.rect.left() + (option.rect.width() - textWidth) / 2, y, textWidth, metrics.height());
        painter->drawText(lineRect, Qt::AlignHCenter | Qt::AlignTop, line);
        y += metrics.height();
    }

    painter->restore();
}

QSize AppDelegate::sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const
{
    return kCellSize;
}
