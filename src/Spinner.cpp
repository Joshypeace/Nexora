#include "Spinner.h"

#include <QConicalGradient>
#include <QPainter>
#include <QVariantAnimation>

Spinner::Spinner(int diameter, QWidget* parent)
    : QWidget(parent)
    , m_rotation(new QVariantAnimation(this))
{
    setFixedSize(diameter, diameter);
    setAttribute(Qt::WA_TransparentForMouseEvents);
    m_rotation->setStartValue(0.0);
    m_rotation->setEndValue(360.0);
    m_rotation->setDuration(900);
    m_rotation->setLoopCount(-1);
    connect(m_rotation, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
        m_angle = value.toReal();
        update();
    });
    hide();
}

void Spinner::setRunning(bool running)
{
    setVisible(running);
    if (running)
        m_rotation->start();
    else
        m_rotation->stop();
}

void Spinner::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const qreal pen = qMax(1.5, width() / 9.0);
    const QRectF arc = QRectF(rect()).adjusted(pen, pen, -pen, -pen);

    QConicalGradient gradient(arc.center(), -m_angle);
    gradient.setColorAt(0.0, QColor(103, 232, 249));
    gradient.setColorAt(0.7, QColor(139, 92, 246, 40));
    gradient.setColorAt(1.0, QColor(139, 92, 246, 0));

    painter.setPen(QPen(QBrush(gradient), pen, Qt::SolidLine, Qt::RoundCap));
    painter.drawArc(arc, static_cast<int>(-m_angle * 16), 270 * 16);
}
