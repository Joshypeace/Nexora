#pragma once

#include "AppEntry.h"

#include <QElapsedTimer>
#include <QHash>
#include <QPixmap>
#include <QWidget>

#include <memory>
#include <vector>

class AppDelegate;
class AppFilterModel;
class AppModel;
class DimOverlay;
class QLabel;
class QLineEdit;
class QListView;
class QStackedWidget;
class Spinner;

// The floating launcher: search field, application grid and minimal controls.
class LauncherWindow : public QWidget {
    Q_OBJECT

public:
    explicit LauncherWindow(QWidget* parent = nullptr);
    ~LauncherWindow() override;

    void summon();
    void dismiss();
    void toggle();
    void toggleFromTray();
    void warmUp();

    void setApps(const std::vector<AppEntry>& apps);
    void updateIcons(const QHash<QString, QImage>& icons);
    void setHiddenIds(const QStringList& ids);
    void setBusy(bool busy);
    void setDimDesktop(bool enabled) { m_dimDesktop = enabled; }

    void browseForApplication();
    void browseForFolder();

signals:
    void shown();
    void addApplicationRequested(const QString& path);
    void addFolderRequested(const QString& path);
    void refreshRequested();
    void hideRequested(const QString& id);
    void launchFailed(const QString& name, const QString& error);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    bool nativeEvent(const QByteArray& eventType, void* message, qintptr* result) override;
    void changeEvent(QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    QWidget* buildHeader();
    QWidget* buildEmptyState();
    QWidget* buildFooter();
    void onQueryChanged(const QString& text);
    void selectRow(int row);
    void launchIndex(const QModelIndex& index, bool elevated = false);
    void launchEntry(const AppEntry& app, bool elevated);
    void showItemMenu(const QPoint& position);
    void showAddMenu(QWidget* anchor);
    void restoreAfterDialog();
    void ensureOverlays();
    void showOverlays();
    void dismissIfInactive(int delayMs);
    void updateGrid();
    void updateState();
    void playRevealAnimation();

    AppModel* m_model;
    AppFilterModel* m_filter;
    AppDelegate* m_delegate;
    std::vector<std::unique_ptr<DimOverlay>> m_overlays; // one per monitor

    QWidget* m_content = nullptr;
    QLineEdit* m_search = nullptr;
    QListView* m_view = nullptr;
    QStackedWidget* m_stack = nullptr;
    QLabel* m_emptyTitle = nullptr;
    QLabel* m_emptyDetail = nullptr;
    Spinner* m_emptySpinner = nullptr;
    QLabel* m_countLabel = nullptr;
    Spinner* m_footerSpinner = nullptr;

    QPixmap m_background;
    QPixmap m_backgroundScaled;
    QElapsedTimer m_sinceDismiss;
    bool m_acrylic = false;
    bool m_busy = false;
    bool m_dimDesktop = true;
    int m_modalDepth = 0; // > 0 while a dialog owned by the launcher is open
};
