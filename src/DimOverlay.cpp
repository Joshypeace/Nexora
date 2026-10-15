#include "DimOverlay.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPropertyAnimation>
#include <QScreen>

DimOverlay::DimOverlay(QWidget* parent)
    : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::WindowDoesNotAcceptFocus)
    , m_fade(new QPropertyAnimation(this, "windowOpacity", this))
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    m_fade->setDuration(160);
    m_fade->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_fade, &QPropertyAnimation::finished, this, [this] {
        if (windowOpacity() <= 0.0)
            hide();
    });
}

void DimOverlay::showOn(QScreen* screen)
{
    m_fade->stop();
    setGeometry(screen->geometry());
    setWindowOpacity(0.0);
    show();
    m_fade->setStartValue(0.0);
    m_fade->setEndValue(1.0);
    m_fade->start();
}

void DimOverlay::fadeOut()
{
    if (!isVisible())
        return;
    m_fade->stop();
    m_fade->setStartValue(windowOpacity());
    m_fade->setEndValue(0.0);
    m_fade->start();
}

void DimOverlay::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.fillRect(rect(), QColor(3, 5, 14, 120));
}

void DimOverlay::mousePressEvent(QMouseEvent* event)
{
    event->accept();
    emit clicked();
}
