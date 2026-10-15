#pragma once

#include <QWidget>

class QVariantAnimation;

// Small rotating arc used while applications are being discovered.
class Spinner : public QWidget {
    Q_OBJECT

public:
    explicit Spinner(int diameter, QWidget* parent = nullptr);

    void setRunning(bool running);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QVariantAnimation* m_rotation;
    qreal m_angle = 0.0;
};
