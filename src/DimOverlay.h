#pragma once

#include <QWidget>

class QPropertyAnimation;
class QScreen;

// Full-screen, non-activating layer that dims the desktop behind the launcher.
// Clicks on it dismiss the launcher instead of reaching the application below.
class DimOverlay : public QWidget {
    Q_OBJECT

public:
    explicit DimOverlay(QWidget* parent = nullptr);

    void showOn(QScreen* screen);
    void fadeOut();

signals:
    void clicked();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    QPropertyAnimation* m_fade;
};
