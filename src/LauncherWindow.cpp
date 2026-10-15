#include "LauncherWindow.h"

#include "AppDelegate.h"
#include "AppLauncher.h"
#include "AppModel.h"
#include "DimOverlay.h"
#include "Spinner.h"
#include "WindowEffects.h"

#include <QAbstractButton>
#include <QApplication>
#include <QCursor>
#include <QFileDialog>
#include <QGraphicsOpacityEffect>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QPainter>
#include <QPropertyAnimation>
#include <QScreen>
#include <QScrollBar>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QTimer>
#include <QVBoxLayout>
#include <QWindow>

#include <windows.h>

#include <algorithm>

namespace {

constexpr QSize kWindowSize{760, 560};
constexpr qreal kImageOpacityOverAcrylic = 0.42;

const char* const kStyleSheet = R"(
QWidget { color: #E6E9F5; }
QLabel#title { font-size: 11pt; font-weight: 600; color: #EEF0FA; }
QLineEdit#search {
    background: rgba(255, 255, 255, 16);
    border: 1px solid rgba(255, 255, 255, 26);
    border-radius: 14px;
    padding: 0 14px 0 6px;
    color: #F3F4FB;
    font-size: 13pt;
    selection-background-color: rgba(139, 92, 246, 140);
}
QLineEdit#search:focus {
    background: rgba(255, 255, 255, 22);
    border: 1px solid rgba(150, 140, 250, 130);
}
QListView { background: transparent; border: none; outline: 0; }
QLabel#emptyTitle { font-size: 12pt; font-weight: 600; color: #E6E9F5; }
QLabel#emptyDetail, QLabel#hint { color: rgba(205, 211, 235, 150); font-size: 8.5pt; }
QScrollBar:vertical { background: transparent; width: 8px; margin: 2px 0 2px 0; }
QScrollBar::handle:vertical { background: rgba(255, 255, 255, 38); border-radius: 4px; min-height: 40px; }
QScrollBar::handle:vertical:hover { background: rgba(255, 255, 255, 70); }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }
)";

enum class Glyph { Plus, Close, Search };

QPixmap glyphPixmap(Glyph glyph, int size, const QColor& color, qreal dpr)
{
    QPixmap pixmap(QSize(size, size) * dpr);
    pixmap.setDevicePixelRatio(dpr);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(color, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);

    const qreal s = size;
    switch (glyph) {
    case Glyph::Plus:
        painter.drawLine(QPointF(s / 2, s * 0.25), QPointF(s / 2, s * 0.75));
        painter.drawLine(QPointF(s * 0.25, s / 2), QPointF(s * 0.75, s / 2));
        break;
    case Glyph::Close:
        painter.drawLine(QPointF(s * 0.3, s * 0.3), QPointF(s * 0.7, s * 0.7));
        painter.drawLine(QPointF(s * 0.7, s * 0.3), QPointF(s * 0.3, s * 0.7));
        break;
    case Glyph::Search:
        painter.drawEllipse(QRectF(s * 0.2, s * 0.2, s * 0.46, s * 0.46));
        painter.drawLine(QPointF(s * 0.6, s * 0.6), QPointF(s * 0.8, s * 0.8));
        break;
    }
    return pixmap;
}

// Small borderless glyph button used for "add" and "close".
class IconButton : public QAbstractButton {
public:
    IconButton(Glyph glyph, const QString& toolTip, QWidget* parent)
        : QAbstractButton(parent)
        , m_glyph(glyph)
    {
        setFixedSize(30, 30);
        setToolTip(toolTip);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::NoFocus);
        setAttribute(Qt::WA_Hover);
    }

protected:
    bool event(QEvent* event) override
    {
        if (event->type() == QEvent::HoverEnter || event->type() == QEvent::HoverLeave)
            update();
        return QAbstractButton::event(event);
    }

    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const bool hovered = underMouse();
        if (hovered || isDown()) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(255, 255, 255, isDown() ? 34 : 20));
            painter.drawRoundedRect(QRectF(rect()), 9, 9);
        }
        constexpr int kGlyph = 18;
        const QColor color = hovered ? QColor(242, 244, 252) : QColor(170, 178, 210);
        painter.drawPixmap(QPoint((width() - kGlyph) / 2, (height() - kGlyph) / 2),
                           glyphPixmap(m_glyph, kGlyph, color, devicePixelRatioF()));
    }

private:
    Glyph m_glyph;
};

QMenu* createMenu(QWidget* parent)
{
    auto* menu = new QMenu(parent);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    return menu;
}

} // namespace

LauncherWindow::LauncherWindow(QWidget* parent)
    // A caption without a system menu keeps DWM's shadow and transitions but stops
    // it from drawing caption buttons into the extended frame.
    : QWidget(parent, Qt::Tool | Qt::WindowStaysOnTopHint | Qt::CustomizeWindowHint | Qt::WindowTitleHint)
    , m_model(new AppModel(this))
    , m_filter(new AppFilterModel(this))
    , m_delegate(new AppDelegate(this))
    , m_background(QStringLiteral(":/background.jpg"))
{
    setAttribute(Qt::WA_TranslucentBackground);
    setWindowTitle(QStringLiteral("Nexora"));
    setFixedSize(kWindowSize);
    m_filter->setSourceModel(m_model);

    m_content = new QWidget(this);
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(m_content);

    auto* layout = new QVBoxLayout(m_content);
    layout->setContentsMargins(22, 16, 22, 12);
    layout->setSpacing(14);
    layout->addWidget(buildHeader());

    m_search = new QLineEdit(m_content);
    m_search->setObjectName(QStringLiteral("search"));
    m_search->setPlaceholderText(tr("Search applications"));
    m_search->setFixedHeight(48);
    m_search->addAction(QIcon(glyphPixmap(Glyph::Search, 20, QColor(160, 168, 205), 2.0)), QLineEdit::LeadingPosition);
    QPalette searchPalette = m_search->palette();
    searchPalette.setColor(QPalette::PlaceholderText, QColor(170, 178, 210, 150));
    m_search->setPalette(searchPalette);
    m_search->installEventFilter(this);
    layout->addWidget(m_search);

    m_view = new QListView(m_content);
    m_view->setViewMode(QListView::IconMode);
    m_view->setMovement(QListView::Static);
    m_view->setResizeMode(QListView::Adjust);
    m_view->setFlow(QListView::LeftToRight);
    m_view->setWrapping(true);
    m_view->setUniformItemSizes(true);
    m_view->setSpacing(0);
    m_view->setSelectionMode(QAbstractItemView::SingleSelection);
    m_view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_view->setDragDropMode(QAbstractItemView::NoDragDrop);
    m_view->setFocusPolicy(Qt::NoFocus);
    m_view->setFrameShape(QFrame::NoFrame);
    m_view->setMouseTracking(true);
    m_view->viewport()->setAttribute(Qt::WA_Hover);
    m_view->viewport()->setAutoFillBackground(false);
    m_view->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_view->verticalScrollBar()->setSingleStep(28);
    m_view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_view->setContextMenuPolicy(Qt::CustomContextMenu);
    m_view->setItemDelegate(m_delegate);
    m_view->setModel(m_filter);
    m_view->viewport()->installEventFilter(this);

    m_stack = new QStackedWidget(m_content);
    m_stack->addWidget(m_view);
    m_stack->addWidget(buildEmptyState());
    layout->addWidget(m_stack, 1);
    layout->addWidget(buildFooter());

    setStyleSheet(QString::fromLatin1(kStyleSheet));

    connect(m_search, &QLineEdit::textChanged, this, &LauncherWindow::onQueryChanged);
    connect(m_view, &QListView::clicked, this, [this](const QModelIndex& index) { launchIndex(index); });
    connect(m_view, &QListView::customContextMenuRequested, this, &LauncherWindow::showItemMenu);
    // Overlays are sized per monitor; if monitors change while open, simply close.
    connect(qApp, &QGuiApplication::screenAdded, this, &LauncherWindow::dismiss);
    connect(qApp, &QGuiApplication::screenRemoved, this, &LauncherWindow::dismiss);
    connect(qApp, &QGuiApplication::primaryScreenChanged, this, &LauncherWindow::dismiss);
    connect(m_filter, &QAbstractItemModel::modelReset, this, &LauncherWindow::updateState);
    connect(m_filter, &QAbstractItemModel::layoutChanged, this, &LauncherWindow::updateState);
    connect(m_filter, &QAbstractItemModel::rowsInserted, this, &LauncherWindow::updateState);
    connect(m_filter, &QAbstractItemModel::rowsRemoved, this, &LauncherWindow::updateState);

    // Create the native window now so DWM styling is in place before the first show.
    winId();
    m_acrylic = WindowEffects::enableAcrylic(this);
    WindowEffects::removeTitleBar(this);
    updateState();
}

LauncherWindow::~LauncherWindow() = default;

// ---------------------------------------------------------------------------
// Construction helpers
// ---------------------------------------------------------------------------

QWidget* LauncherWindow::buildHeader()
{
    auto* header = new QWidget(m_content);
    auto* row = new QHBoxLayout(header);
    row->setContentsMargins(2, 0, 0, 0);
    row->setSpacing(10);

    auto* logo = new QLabel(header);
    constexpr int kLogo = 22;
    QPixmap logoPixmap = QPixmap(QStringLiteral(":/logo.png"))
                             .scaled(kLogo * 3, kLogo * 3, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    logoPixmap.setDevicePixelRatio(3.0);
    logo->setPixmap(logoPixmap);
    logo->setFixedSize(kLogo, kLogo);

    auto* title = new QLabel(QStringLiteral("Nexora"), header);
    title->setObjectName(QStringLiteral("title"));

    auto* add = new IconButton(Glyph::Plus, tr("Add applications"), header);
    auto* close = new IconButton(Glyph::Close, tr("Close (Esc)"), header);
    connect(add, &QAbstractButton::clicked, this, [this, add] { showAddMenu(add); });
    connect(close, &QAbstractButton::clicked, this, &LauncherWindow::dismiss);

    row->addWidget(logo);
    row->addWidget(title);
    row->addStretch(1);
    row->addWidget(add);
    row->addWidget(close);
    return header;
}

QWidget* LauncherWindow::buildEmptyState()
{
    auto* page = new QWidget(m_content);
    auto* column = new QVBoxLayout(page);
    column->setSpacing(8);
    column->addStretch(1);

    m_emptySpinner = new Spinner(22, page);
    column->addWidget(m_emptySpinner, 0, Qt::AlignHCenter);

    m_emptyTitle = new QLabel(page);
    m_emptyTitle->setObjectName(QStringLiteral("emptyTitle"));
    m_emptyTitle->setAlignment(Qt::AlignCenter);
    column->addWidget(m_emptyTitle);

    m_emptyDetail = new QLabel(page);
    m_emptyDetail->setObjectName(QStringLiteral("emptyDetail"));
    m_emptyDetail->setAlignment(Qt::AlignCenter);
    column->addWidget(m_emptyDetail);

    column->addStretch(2);
    return page;
}

QWidget* LauncherWindow::buildFooter()
{
    auto* footer = new QWidget(m_content);
    auto* row = new QHBoxLayout(footer);
    row->setContentsMargins(4, 0, 4, 0);
    row->setSpacing(8);

    m_footerSpinner = new Spinner(12, footer);
    m_countLabel = new QLabel(footer);
    m_countLabel->setObjectName(QStringLiteral("hint"));

    auto* keys = new QLabel(footer);
    keys->setObjectName(QStringLiteral("hint"));
    keys->setTextFormat(Qt::RichText);
    const QString key = QStringLiteral("<span style='color:#D4D9F0'>%1</span>&nbsp;%2");
    keys->setText(key.arg(QStringLiteral("↑↓←→"), tr("navigate")) + QStringLiteral("&nbsp;&nbsp;&nbsp;")
                  + key.arg(QStringLiteral("↵"), tr("open")) + QStringLiteral("&nbsp;&nbsp;&nbsp;")
                  + key.arg(QStringLiteral("Esc"), tr("close")));

    row->addWidget(m_footerSpinner);
    row->addWidget(m_countLabel);
    row->addStretch(1);
    row->addWidget(keys);
    return footer;
}

// ---------------------------------------------------------------------------
// Showing and hiding
// ---------------------------------------------------------------------------

void LauncherWindow::summon()
{
    if (isVisible()) {
        activateWindow();
        m_search->setFocus();
        return;
    }

    m_search->clear();
    selectRow(0);
    m_view->scrollToTop();

    QScreen* screen = QGuiApplication::screenAt(QCursor::pos());
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    windowHandle()->setScreen(screen);

    const QRect area = screen->availableGeometry();
    const QPoint topLeft(area.center().x() - width() / 2, area.top() + (area.height() - height()) * 2 / 5);
    move(topLeft);
    WindowEffects::removeTitleBar(this); // re-measure for the target monitor's DPI
    move(topLeft);

    if (m_dimDesktop)
        showOverlays();

    show();
    raise();
    activateWindow();
    SetForegroundWindow(reinterpret_cast<HWND>(winId()));
    m_search->setFocus();
    playRevealAnimation();
    emit shown();
}

void LauncherWindow::dismiss()
{
    // Never hide the owner of an open file dialog.
    if (!isVisible() || m_modalDepth > 0)
        return;
    for (const auto& overlay : m_overlays)
        overlay->fadeOut();
    // Nexora idles in the tray between uses; hand unused pages back to Windows once the
    // fade-out has finished. They are faulted back in transparently on the next show.
    QTimer::singleShot(1000, this, [this] {
        if (!isVisible())
            SetProcessWorkingSetSize(GetCurrentProcess(), static_cast<SIZE_T>(-1), static_cast<SIZE_T>(-1));
    });
    hide();
    m_sinceDismiss.start();
}

void LauncherWindow::warmUp()
{
    // Render once off-screen so fonts, style polish, layouts and icon pixmaps are ready
    // before the first hotkey press instead of being initialised while the user waits.
    if (isVisible())
        return;
    grab();
    ensureOverlays();
    for (const auto& overlay : m_overlays)
        overlay->winId();
}

void LauncherWindow::ensureOverlays()
{
    const auto count = static_cast<size_t>(QGuiApplication::screens().size());
    while (m_overlays.size() < count) {
        auto overlay = std::make_unique<DimOverlay>();
        connect(overlay.get(), &DimOverlay::clicked, this, &LauncherWindow::dismiss);
        m_overlays.push_back(std::move(overlay));
    }
    m_overlays.resize(count);
}

void LauncherWindow::showOverlays()
{
    // Dim (and catch clicks on) every monitor, so a click anywhere outside the
    // launcher dismisses it instead of reaching the application underneath.
    ensureOverlays();
    const QList<QScreen*> screens = QGuiApplication::screens();
    for (qsizetype i = 0; i < screens.size(); ++i)
        m_overlays[static_cast<size_t>(i)]->showOn(screens[i]);
}

void LauncherWindow::toggle()
{
    if (isVisible())
        dismiss();
    else
        summon();
}

void LauncherWindow::toggleFromTray()
{
    // Clicking the tray icon first deactivates (and so dismisses) the launcher;
    // don't immediately bring it back in that case.
    if (!isVisible() && m_sinceDismiss.isValid() && m_sinceDismiss.elapsed() < 300)
        return;
    toggle();
}

void LauncherWindow::playRevealAnimation()
{
    auto* effect = new QGraphicsOpacityEffect(m_content);
    effect->setOpacity(0.0);
    m_content->setGraphicsEffect(effect);

    auto* fade = new QPropertyAnimation(effect, "opacity", effect);
    fade->setDuration(170);
    fade->setStartValue(0.0);
    fade->setEndValue(1.0);
    fade->setEasingCurve(QEasingCurve::OutCubic);
    // Drop the effect afterwards; rendering through it costs an off-screen pass.
    connect(fade, &QPropertyAnimation::finished, m_content, [this, effect] {
        if (m_content->graphicsEffect() == effect)
            m_content->setGraphicsEffect(nullptr);
    }, Qt::QueuedConnection);
    fade->start();
}

// ---------------------------------------------------------------------------
// Data
// ---------------------------------------------------------------------------

void LauncherWindow::setApps(const std::vector<AppEntry>& apps)
{
    const QString currentId = m_view->currentIndex().data(AppModel::IdRole).toString();
    m_model->setApps(apps, devicePixelRatioF());

    int row = 0;
    for (int i = 0; i < m_filter->rowCount() && !currentId.isEmpty(); ++i) {
        if (m_filter->index(i, 0).data(AppModel::IdRole).toString() == currentId) {
            row = i;
            break;
        }
    }
    selectRow(row);
    updateState();
}

void LauncherWindow::updateIcons(const QHash<QString, QImage>& icons)
{
    m_model->updateIcons(icons, devicePixelRatioF());
}

void LauncherWindow::setHiddenIds(const QStringList& ids)
{
    m_filter->setHiddenIds(ids);
    selectRow(m_view->currentIndex().isValid() ? m_view->currentIndex().row() : 0);
    updateState();
}

void LauncherWindow::setBusy(bool busy)
{
    m_busy = busy;
    updateState();
}

void LauncherWindow::onQueryChanged(const QString& text)
{
    m_filter->setQuery(text);
    selectRow(0);
    m_view->scrollToTop();
    updateState();
}

void LauncherWindow::selectRow(int row)
{
    const int count = m_filter->rowCount();
    if (count == 0) {
        m_view->setCurrentIndex({});
        return;
    }
    const QModelIndex index = m_filter->index(std::clamp(row, 0, count - 1), 0);
    m_view->setCurrentIndex(index);
    m_view->scrollTo(index);
}

void LauncherWindow::updateGrid()
{
    // QListView lays wrapped items out within the maximum viewport minus a scroll bar's
    // extent (whether or not one is shown), and wraps when a row fills it exactly. Size the
    // cells against that width so the column count doesn't change with the result count.
    // Same query as QListViewPrivate::prepareItemsLayout, so the style sheet's width applies.
    const int scrollBar = m_view->style()->pixelMetric(QStyle::PM_ScrollBarExtent, nullptr, m_view->verticalScrollBar());
    const int width = m_view->maximumViewportSize().width() - scrollBar - 2;
    if (width <= 0)
        return;
    const int columns = std::max(1, width / AppDelegate::kCellSize.width());
    const QSize cell(width / columns, AppDelegate::kCellSize.height());
    if (m_view->gridSize() != cell)
        m_view->setGridSize(cell);
}

void LauncherWindow::updateState()
{
    const int visible = m_filter->rowCount();
    const bool hasApps = m_model->rowCount() > 0;
    const bool emptyLoading = visible == 0 && !hasApps && m_busy;

    m_stack->setCurrentIndex(visible > 0 ? 0 : 1);
    m_emptySpinner->setRunning(emptyLoading);
    m_footerSpinner->setRunning(m_busy && !emptyLoading);

    if (visible == 0) {
        if (emptyLoading) {
            m_emptyTitle->setText(tr("Discovering applications…"));
            m_emptyDetail->setText(tr("This only takes a moment."));
        } else if (!hasApps) {
            m_emptyTitle->setText(tr("No applications found"));
            m_emptyDetail->setText(tr("Use + to add an application or a folder of portable apps."));
        } else {
            const QString query = m_emptyTitle->fontMetrics().elidedText(m_search->text().trimmed(), Qt::ElideRight, 360);
            m_emptyTitle->setText(tr("No matches for “%1”").arg(query));
            m_emptyDetail->setText(tr("Try another name, or press Esc to close."));
        }
    }

    if (m_busy && !hasApps)
        m_countLabel->setText(tr("Scanning…"));
    else if (m_filter->query().isEmpty())
        m_countLabel->setText(visible == 1 ? tr("1 application") : tr("%1 applications").arg(visible));
    else
        m_countLabel->setText(visible == 1 ? tr("1 result") : tr("%1 results").arg(visible));
}

// ---------------------------------------------------------------------------
// Launching and menus
// ---------------------------------------------------------------------------

void LauncherWindow::launchIndex(const QModelIndex& index, bool elevated)
{
    if (const AppEntry* app = m_model->entry(m_filter->mapToSource(index)))
        launchEntry(AppEntry(*app), elevated);
}

void LauncherWindow::launchEntry(const AppEntry& app, bool elevated)
{
    AppLauncher::launchAsync(app, elevated, this, [this, name = app.name](const AppLauncher::Result& result) {
        if (!result.ok)
            emit launchFailed(name, result.error);
    });
    dismiss();
}

void LauncherWindow::showItemMenu(const QPoint& position)
{
    const QModelIndex index = m_view->indexAt(position);
    const AppEntry* entry = m_model->entry(m_filter->mapToSource(index));
    if (!entry)
        return;
    selectRow(index.row());

    // Copy: a background refresh may replace the model while the menu is open.
    const AppEntry app = *entry;
    const bool packaged = app.kind == AppKind::Packaged;

    QMenu* menu = createMenu(this);
    menu->addAction(tr("Open"), this, [this, app] { launchEntry(app, false); });
    if (AppLauncher::canElevate(app))
        menu->addAction(tr("Run as administrator"), this, [this, app] { launchEntry(app, true); });
    if (!packaged) {
        menu->addAction(tr("Open file location"), this, [this, app] {
            if (AppLauncher::revealInExplorer(app))
                dismiss();
        });
    }
    menu->addSeparator();
    menu->addAction(tr("Hide from Nexora"), this, [this, id = app.id] { emit hideRequested(id); });
    menu->popup(m_view->viewport()->mapToGlobal(position));
}

void LauncherWindow::showAddMenu(QWidget* anchor)
{
    QMenu* menu = createMenu(this);
    menu->addAction(tr("Add application…"), this, &LauncherWindow::browseForApplication);
    menu->addAction(tr("Add folder of apps…"), this, &LauncherWindow::browseForFolder);
    menu->addSeparator();
    menu->addAction(tr("Refresh applications"), this, &LauncherWindow::refreshRequested);
    menu->popup(anchor->mapToGlobal(QPoint(anchor->width() - menu->sizeHint().width(), anchor->height() + 6)));
}

void LauncherWindow::browseForApplication()
{
    ++m_modalDepth;
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Add application"), QStandardPaths::writableLocation(QStandardPaths::DesktopLocation),
        tr("Applications and shortcuts (*.exe *.lnk *.url *.appref-ms *.bat *.cmd);;All files (*)"));
    --m_modalDepth;
    restoreAfterDialog();
    if (!path.isEmpty())
        emit addApplicationRequested(path);
}

void LauncherWindow::browseForFolder()
{
    ++m_modalDepth;
    const QString path = QFileDialog::getExistingDirectory(this, tr("Add a folder of portable applications"));
    --m_modalDepth;
    restoreAfterDialog();
    if (!path.isEmpty())
        emit addFolderRequested(path);
}

void LauncherWindow::restoreAfterDialog()
{
    activateWindow();
    m_search->setFocus();
    // If the user switched to another app meanwhile, the foreground lock can refuse
    // re-activation; then no deactivation event will follow, so check explicitly.
    dismissIfInactive(200);
}

// ---------------------------------------------------------------------------
// Events
// ---------------------------------------------------------------------------

bool LauncherWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_view->viewport() && event->type() == QEvent::Resize) {
        updateGrid();
        return false;
    }
    if (watched != m_search || event->type() != QEvent::KeyPress)
        return QWidget::eventFilter(watched, event);

    auto* key = static_cast<QKeyEvent*>(event);
    const int current = m_view->currentIndex().isValid() ? m_view->currentIndex().row() : -1;
    switch (key->key()) {
    case Qt::Key_Escape:
        dismiss();
        return true;
    case Qt::Key_Return:
    case Qt::Key_Enter: {
        const bool elevated = key->modifiers().testFlags(Qt::ControlModifier | Qt::ShiftModifier);
        launchIndex(current >= 0 ? m_view->currentIndex() : m_filter->index(0, 0), elevated);
        return true;
    }
    case Qt::Key_Tab:
        selectRow(current + 1);
        return true;
    case Qt::Key_Backtab:
        selectRow(current - 1);
        return true;
    case Qt::Key_Left:
    case Qt::Key_Right:
        if (key->modifiers().testFlag(Qt::ShiftModifier))
            return false; // keep text selection in the search field
        [[fallthrough]];
    case Qt::Key_Up:
    case Qt::Key_Down:
    case Qt::Key_PageUp:
    case Qt::Key_PageDown:
        if (current < 0)
            selectRow(0);
        else
            QCoreApplication::sendEvent(m_view, key);
        return true;
    default:
        return false;
    }
}

bool LauncherWindow::nativeEvent(const QByteArray& eventType, void* message, qintptr* result)
{
    const auto* msg = static_cast<const MSG*>(message);
    switch (msg->message) {
    case WM_NCHITTEST:
        // The whole window is client area: no resize borders, no caption dragging.
        *result = HTCLIENT;
        return true;
    case WM_CLOSE:
        // Alt+F4 on the open launcher just dismisses it. A close request while it is
        // hidden comes from outside (an installer upgrading or removing Nexora, or
        // taskkill), so shut the whole tray app down cleanly.
        if (isVisible())
            dismiss();
        else
            QCoreApplication::quit();
        *result = 0;
        return true;
    case WM_DPICHANGED:
        QTimer::singleShot(0, this, [this] {
            WindowEffects::removeTitleBar(this);
            m_backgroundScaled = {};
            m_model->setDevicePixelRatio(devicePixelRatioF());
        });
        break;
    default:
        break;
    }
    return QWidget::nativeEvent(eventType, message, result);
}

void LauncherWindow::changeEvent(QEvent* event)
{
    QWidget::changeEvent(event);
    if (event->type() != QEvent::ActivationChange || isActiveWindow() || !isVisible())
        return;

    // Clicking another application dismisses the launcher.
    dismissIfInactive(0);
}

void LauncherWindow::dismissIfInactive(int delayMs)
{
    // Deferred so transient activation changes (popups, our own dialogs) settle first.
    QTimer::singleShot(delayMs, this, [this] {
        if (isVisible() && !isActiveWindow() && m_modalDepth == 0 && !QApplication::activePopupWidget())
            dismiss();
    });
}

void LauncherWindow::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    const QRectF bounds = rect();
    const qreal dpr = devicePixelRatioF();

    const QSize target = (QSizeF(size()) * dpr).toSize();
    if (m_backgroundScaled.isNull() || m_backgroundScaled.size() != target) {
        m_backgroundScaled = m_background.scaled(target, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation)
                                 .copy(QRect(QPoint(0, 0), target));
        m_backgroundScaled.setDevicePixelRatio(dpr);
    }

    // Background artwork beneath a dark overlay; over acrylic it is partly
    // transparent so the blurred desktop shows through.
    painter.setOpacity(m_acrylic ? kImageOpacityOverAcrylic : 1.0);
    painter.drawPixmap(bounds.topLeft(), m_backgroundScaled);
    painter.setOpacity(1.0);

    QLinearGradient overlay(bounds.topLeft(), bounds.bottomRight());
    overlay.setColorAt(0.0, QColor(7, 9, 22, m_acrylic ? 168 : 205));
    overlay.setColorAt(0.6, QColor(9, 11, 26, m_acrylic ? 150 : 190));
    overlay.setColorAt(1.0, QColor(12, 14, 32, m_acrylic ? 140 : 175));
    painter.fillRect(bounds, overlay);

    // Hairline highlight along the top edge for a glassy finish.
    painter.setRenderHint(QPainter::Antialiasing);
    QLinearGradient highlight(bounds.topLeft(), bounds.topRight());
    highlight.setColorAt(0.0, QColor(255, 255, 255, 0));
    highlight.setColorAt(0.5, QColor(255, 255, 255, 34));
    highlight.setColorAt(1.0, QColor(255, 255, 255, 0));
    painter.setPen(QPen(QBrush(highlight), 1.0));
    painter.drawLine(QPointF(bounds.left(), 0.5), QPointF(bounds.right(), 0.5));
}
